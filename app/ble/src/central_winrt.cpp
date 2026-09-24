// Windows Bluetooth LE central on Windows.Devices.Bluetooth (C++/WinRT, part of the Windows SDK).
#include "central.h"

#include <windows.h>
#include <combaseapi.h>

#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Radios.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>

#include <condition_variable>
#include <cstdio>
#include <exception>
#include <mutex>
#include <thread>
#include <type_traits>

namespace djivcam::ble::detail {
namespace {

namespace bt = winrt::Windows::Devices::Bluetooth;
namespace adv = winrt::Windows::Devices::Bluetooth::Advertisement;
namespace gatt = winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
using winrt::Windows::Devices::Radios::RadioState;
using winrt::Windows::Storage::Streams::DataReader;
using winrt::Windows::Storage::Streams::DataWriter;
using winrt::Windows::Storage::Streams::IBuffer;

// WinRT's blocking get() is not allowed on single-threaded apartments (a Qt GUI thread is one).
// The process-wide multithreaded apartment is kept alive, so threads that never initialized COM
// run in it implicitly; blocking calls made from an STA thread move to such a helper thread.
template <typename F>
auto blocking(F&& operation) -> std::invoke_result_t<F&> {
    static const bool mta_kept_alive = [] {
        CO_MTA_USAGE_COOKIE cookie{};
        return SUCCEEDED(CoIncrementMTAUsage(&cookie));  // never released: lives as long as the process
    }();
    (void)mta_kept_alive;
    APTTYPE type{};
    APTTYPEQUALIFIER qualifier{};
    const bool sta = SUCCEEDED(CoGetApartmentType(&type, &qualifier)) && (type == APTTYPE_STA || type == APTTYPE_MAINSTA);
    if (!sta) {
        return operation();
    }
    using Result = std::invoke_result_t<F&>;
    std::exception_ptr failure;
    if constexpr (std::is_void_v<Result>) {
        std::thread([&] {
            try {
                operation();
            } catch (...) {
                failure = std::current_exception();
            }
        }).join();
        if (failure) {
            std::rethrow_exception(failure);
        }
    } else {
        Result result{};
        std::thread([&] {
            try {
                result = operation();
            } catch (...) {
                failure = std::current_exception();
            }
        }).join();
        if (failure) {
            std::rethrow_exception(failure);
        }
        return result;
    }
}

std::string format_address(std::uint64_t address) {
    char text[18];
    std::snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x", static_cast<unsigned>(address >> 40) & 0xFF,
                  static_cast<unsigned>(address >> 32) & 0xFF, static_cast<unsigned>(address >> 24) & 0xFF,
                  static_cast<unsigned>(address >> 16) & 0xFF, static_cast<unsigned>(address >> 8) & 0xFF,
                  static_cast<unsigned>(address) & 0xFF);
    return text;
}

std::uint64_t parse_address(const std::string& address) {
    std::uint64_t value = 0;
    for (char c : address) {
        if (c == ':') {
            continue;
        }
        value = (value << 4) | static_cast<std::uint64_t>(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
    }
    return value;
}

Bytes to_bytes(const IBuffer& buffer) {
    Bytes bytes(buffer.Length());
    DataReader::FromBuffer(buffer).ReadBytes(bytes);
    return bytes;
}

winrt::guid to_guid(ShortUuid uuid) { return bt::BluetoothUuidHelper::FromShortId(uuid); }

std::string describe(const winrt::hresult_error& error) {
    return winrt::to_string(error.message()) + " (" + std::to_string(static_cast<std::int32_t>(error.code())) + ")";
}

class WinrtLink final : public GattLink {
public:
    explicit WinrtLink(bt::BluetoothLEDevice device) : device_(std::move(device)), fence_(std::make_shared<Fence>()) {}
    ~WinrtLink() override { disconnect(); }

    bool open(std::string* error) {
        try {
            // Without a session that maintains the connection, Windows drops the link as soon as
            // no GATT operation is pending.
            session_ = gatt::GattSession::FromDeviceIdAsync(device_.BluetoothDeviceId()).get();
            session_.MaintainConnection(true);
            const auto result = device_.GetGattServicesAsync(bt::BluetoothCacheMode::Uncached).get();
            if (result.Status() != gatt::GattCommunicationStatus::Success) {
                *error = "reading the camera's services failed";
                return false;
            }
            for (const auto& service : result.Services()) {
                services_.push_back(service);
            }
            return true;
        } catch (const winrt::hresult_error& failure) {
            *error = describe(failure);
            return false;
        }
    }

    bool subscribe(ShortUuid service, ShortUuid characteristic, Notify callback) override {
        return blocking([&] {
            try {
                const auto target = find(service, characteristic);
                if (!target) {
                    return false;
                }
                const auto properties = target.CharacteristicProperties();
                gatt::GattClientCharacteristicConfigurationDescriptorValue mode;
                if ((properties & gatt::GattCharacteristicProperties::Notify) != gatt::GattCharacteristicProperties::None) {
                    mode = gatt::GattClientCharacteristicConfigurationDescriptorValue::Notify;
                } else if ((properties & gatt::GattCharacteristicProperties::Indicate) != gatt::GattCharacteristicProperties::None) {
                    mode = gatt::GattClientCharacteristicConfigurationDescriptorValue::Indicate;
                } else {
                    return false;
                }
                auto token = target.ValueChanged(
                    [fence = fence_, callback = std::move(callback)](const gatt::GattCharacteristic&,
                                                                     const gatt::GattValueChangedEventArgs& args) {
                        const Bytes value = to_bytes(args.CharacteristicValue());
                        std::lock_guard lock(fence->mutex);
                        if (fence->open) {
                            callback(value);
                        }
                    });
                {
                    std::lock_guard lock(mutex_);
                    subscriptions_.push_back({target, token});
                }
                return target.WriteClientCharacteristicConfigurationDescriptorAsync(mode).get() ==
                       gatt::GattCommunicationStatus::Success;
            } catch (const winrt::hresult_error&) {
                return false;
            }
        });
    }

    bool write(ShortUuid service, ShortUuid characteristic, const Bytes& value, bool with_response) override {
        return blocking([&] {
            try {
                const auto target = find(service, characteristic);
                if (!target) {
                    return false;
                }
                DataWriter writer;
                writer.WriteBytes(value);
                const auto option = with_response ? gatt::GattWriteOption::WriteWithResponse : gatt::GattWriteOption::WriteWithoutResponse;
                return target.WriteValueWithResultAsync(writer.DetachBuffer(), option).get().Status() ==
                       gatt::GattCommunicationStatus::Success;
            } catch (const winrt::hresult_error&) {
                return false;
            }
        });
    }

    bool connected() const override {
        try {
            return device_ && device_.ConnectionStatus() == bt::BluetoothConnectionStatus::Connected;
        } catch (const winrt::hresult_error&) {
            return false;
        }
    }

    void disconnect() override {
        {
            std::lock_guard lock(fence_->mutex);  // waits for a running callback, blocks later ones
            fence_->open = false;
        }
        std::lock_guard lock(mutex_);
        try {
            for (auto& [characteristic, token] : subscriptions_) {
                characteristic.ValueChanged(token);
            }
            subscriptions_.clear();
            characteristics_.clear();
            services_.clear();
            if (session_) {
                session_.Close();  // Windows disconnects once nothing holds the device open
                session_ = nullptr;
            }
            if (device_) {
                device_.Close();
                device_ = nullptr;
            }
        } catch (const winrt::hresult_error&) {
        }
    }

private:
    // Keeps notification callbacks from running after disconnect() returned.
    struct Fence {
        std::mutex mutex;
        bool open = true;
    };
    struct Subscription {
        gatt::GattCharacteristic characteristic;
        winrt::event_token token;
    };

    // Looks the characteristic up once and caches it; the writer thread and others call this.
    gatt::GattCharacteristic find(ShortUuid service, ShortUuid characteristic) {
        std::lock_guard lock(mutex_);
        const std::uint32_t key = (std::uint32_t{service} << 16) | characteristic;
        if (const auto it = characteristics_.find(key); it != characteristics_.end()) {
            return it->second;
        }
        for (const auto& candidate : services_) {
            if (candidate.Uuid() != to_guid(service)) {
                continue;
            }
            const auto result = candidate.GetCharacteristicsForUuidAsync(to_guid(characteristic), bt::BluetoothCacheMode::Uncached).get();
            if (result.Status() == gatt::GattCommunicationStatus::Success && result.Characteristics().Size() > 0) {
                return characteristics_.emplace(key, result.Characteristics().GetAt(0)).first->second;
            }
        }
        return nullptr;
    }

    bt::BluetoothLEDevice device_;
    gatt::GattSession session_{nullptr};
    std::shared_ptr<Fence> fence_;
    std::mutex mutex_;
    std::vector<gatt::GattDeviceService> services_;
    std::map<std::uint32_t, gatt::GattCharacteristic> characteristics_;
    std::vector<Subscription> subscriptions_;
};

class WinrtCentral final : public Central {
public:
    void scan(std::chrono::milliseconds timeout, const std::function<bool(const Advertisement&)>& on_advertisement,
              std::stop_token stop) override {
        auto state = std::make_shared<ScanState>();
        blocking([&] {
            try {
                watch(state, timeout, on_advertisement, stop);
            } catch (const winrt::hresult_error&) {
                std::lock_guard lock(state->mutex);
                state->done = true;  // e.g. Bluetooth turned off: report nothing more
            }
        });
    }

    std::unique_ptr<GattLink> connect(const std::string& address, std::string* error) override {
        return blocking([&]() -> std::unique_ptr<GattLink> {
            try {
                auto device = bt::BluetoothLEDevice::FromBluetoothAddressAsync(parse_address(address)).get();
                if (!device) {
                    *error = "the camera is not reachable over Bluetooth";
                    return nullptr;
                }
                auto link = std::make_unique<WinrtLink>(std::move(device));
                return link->open(error) ? std::move(link) : nullptr;
            } catch (const winrt::hresult_error& failure) {
                *error = describe(failure);
                return nullptr;
            }
        });
    }

private:
    // Shared with the event handler, which may still be running briefly after Stop().
    struct ScanState {
        std::mutex mutex;
        std::condition_variable_any changed;
        bool done = false;
        std::map<std::uint64_t, Advertisement> seen;
    };

    static void watch(const std::shared_ptr<ScanState>& state, std::chrono::milliseconds timeout,
                      const std::function<bool(const Advertisement&)>& on_advertisement, std::stop_token stop) {
        adv::BluetoothLEAdvertisementWatcher watcher;
        watcher.ScanningMode(adv::BluetoothLEScanningMode::Active);  // scan responses carry the name
        const auto token = watcher.Received(
            [state, &on_advertisement](const adv::BluetoothLEAdvertisementWatcher&,
                                       const adv::BluetoothLEAdvertisementReceivedEventArgs& args) {
                std::lock_guard lock(state->mutex);
                if (state->done) {
                    return;  // scan() may have returned: on_advertisement is gone
                }
                Advertisement& seen = state->seen[args.BluetoothAddress()];
                seen.address = format_address(args.BluetoothAddress());
                seen.rssi = args.RawSignalStrengthInDBm();
                if (auto name = winrt::to_string(args.Advertisement().LocalName()); !name.empty()) {
                    seen.name = std::move(name);
                }
                for (const auto& section : args.Advertisement().ManufacturerData()) {
                    seen.manufacturer_data[section.CompanyId()] = to_bytes(section.Data());
                }
                if (on_advertisement(seen)) {
                    state->done = true;
                    state->changed.notify_all();
                }
            });
        watcher.Start();
        {
            std::unique_lock lock(state->mutex);
            state->changed.wait_for(lock, stop, timeout, [&] { return state->done; });
            state->done = true;
        }
        watcher.Stop();
        watcher.Received(token);
    }
};

}  // namespace

std::unique_ptr<Central> Central::create() {
    return blocking([]() -> std::unique_ptr<Central> {
        try {
            const auto adapter = bt::BluetoothAdapter::GetDefaultAsync().get();
            if (!adapter || !adapter.IsLowEnergySupported()) {
                return nullptr;
            }
            const auto radio = adapter.GetRadioAsync().get();
            if (!radio || radio.State() != RadioState::On) {
                return nullptr;
            }
            return std::make_unique<WinrtCentral>();
        } catch (const winrt::hresult_error&) {
            return nullptr;
        }
    });
}

}  // namespace djivcam::ble::detail
