// Linux Bluetooth LE central on SimpleBLE (BlueZ over D-Bus), until our own BlueZ implementation
// replaces it (SimpleBLE is BUSL-1.1 licensed).
#include "central.h"

#include <simpleble/SimpleBLE.h>

#include <condition_variable>
#include <cstdio>
#include <exception>
#include <mutex>
#include <optional>

namespace djivcam::ble::detail {
namespace {

std::string uuid_text(ShortUuid uuid) {
    char text[37];
    std::snprintf(text, sizeof(text), "0000%04x-0000-1000-8000-00805f9b34fb", uuid);
    return text;
}

class SimpleBleLink final : public GattLink {
public:
    explicit SimpleBleLink(SimpleBLE::Peripheral peripheral) : peripheral_(std::move(peripheral)) {}
    ~SimpleBleLink() override { disconnect(); }

    bool subscribe(ShortUuid service, ShortUuid characteristic, Notify callback) override {
        try {
            peripheral_.notify(uuid_text(service), uuid_text(characteristic),
                               [callback = std::move(callback)](SimpleBLE::ByteArray data) { callback(Bytes(data.begin(), data.end())); });
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    bool write(ShortUuid service, ShortUuid characteristic, const Bytes& value, bool with_response) override {
        try {
            const SimpleBLE::ByteArray data(value.begin(), value.end());
            if (with_response) {
                peripheral_.write_request(uuid_text(service), uuid_text(characteristic), data);
            } else {
                peripheral_.write_command(uuid_text(service), uuid_text(characteristic), data);
            }
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    bool connected() const override {
        try {
            return const_cast<SimpleBLE::Peripheral&>(peripheral_).is_connected();
        } catch (const std::exception&) {
            return false;
        }
    }

    void disconnect() override {
        try {
            if (peripheral_.is_connected()) {
                peripheral_.disconnect();
            }
        } catch (const std::exception&) {
        }
    }

private:
    SimpleBLE::Peripheral peripheral_;
};

class SimpleBleCentral final : public Central {
public:
    explicit SimpleBleCentral(SimpleBLE::Adapter adapter) : adapter_(std::move(adapter)) {}

    void scan(std::chrono::milliseconds timeout, const std::function<bool(const Advertisement&)>& on_advertisement,
              std::stop_token stop) override {
        std::mutex mutex;
        std::condition_variable_any changed;
        bool done = false;
        auto consider = [&](SimpleBLE::Peripheral peripheral) {
            Advertisement seen;
            seen.name = peripheral.identifier();
            seen.address = normalize_address(peripheral.address());
            seen.rssi = peripheral.rssi();
            for (const auto& [company, data] : peripheral.manufacturer_data()) {
                seen.manufacturer_data[company] = Bytes(data.begin(), data.end());
            }
            std::lock_guard lock(mutex);
            peripherals_.insert_or_assign(seen.address, peripheral);
            if (!done && on_advertisement(seen)) {
                done = true;
                changed.notify_all();
            }
        };
        adapter_.set_callback_on_scan_found(consider);
        adapter_.set_callback_on_scan_updated(consider);
        adapter_.scan_start();
        {
            std::unique_lock lock(mutex);
            changed.wait_for(lock, stop, timeout, [&] { return done; });
            done = true;
        }
        adapter_.scan_stop();
        adapter_.set_callback_on_scan_found(nullptr);
        adapter_.set_callback_on_scan_updated(nullptr);
    }

    std::unique_ptr<GattLink> connect(const std::string& address, std::string* error) override {
        const auto it = peripherals_.find(normalize_address(address));
        if (it == peripherals_.end()) {
            *error = "camera not scanned";
            return nullptr;
        }
        try {
            it->second.connect();
            return std::make_unique<SimpleBleLink>(it->second);
        } catch (const std::exception& failure) {
            *error = failure.what();
            return nullptr;
        }
    }

private:
    SimpleBLE::Adapter adapter_;
    std::map<std::string, SimpleBLE::Peripheral> peripherals_;  // seen while scanning, by address
};

}  // namespace

std::unique_ptr<Central> Central::create() {
    try {
        if (!SimpleBLE::Adapter::bluetooth_enabled()) {
            return nullptr;
        }
        auto adapters = SimpleBLE::Adapter::get_adapters();
        return adapters.empty() ? nullptr : std::make_unique<SimpleBleCentral>(adapters.front());
    } catch (const std::exception&) {
        return nullptr;
    }
}

}  // namespace djivcam::ble::detail
