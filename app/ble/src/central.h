// The small Bluetooth LE central role the camera session needs: scan advertisements, connect,
// subscribe to notifications and write. One implementation per platform:
//   central_winrt.cpp      Windows (Windows.Devices.Bluetooth through C++/WinRT)
//   central_simpleble.cpp  Linux, until a BlueZ implementation replaces it
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace djivcam::ble::detail {

using Bytes = std::vector<std::uint8_t>;

// Bluetooth SIG 16-bit UUID (0000xxxx-0000-1000-8000-00805f9b34fb); the camera uses only these.
using ShortUuid = std::uint16_t;

struct Advertisement {
    std::string name;
    std::string address;  // lowercase "aa:bb:cc:dd:ee:ff"
    int rssi = 0;
    std::map<std::uint16_t, Bytes> manufacturer_data;  // by company ID
};

class GattLink {
public:
    using Notify = std::function<void(const Bytes&)>;

    virtual ~GattLink() = default;
    // Enables notifications (or indications); false if the characteristic has neither.
    // `callback` runs on a Bluetooth stack thread.
    virtual bool subscribe(ShortUuid service, ShortUuid characteristic, Notify callback) = 0;
    virtual bool write(ShortUuid service, ShortUuid characteristic, const Bytes& value, bool with_response) = 0;
    virtual bool connected() const = 0;
    virtual void disconnect() = 0;
};

class Central {
public:
    virtual ~Central() = default;

    // Null when there is no Bluetooth adapter or its radio is off.
    static std::unique_ptr<Central> create();

    // Reports advertisements (merged per device, so a name from a scan response is kept) until
    // `on_advertisement` returns true or `timeout` expires.
    virtual void scan(std::chrono::milliseconds timeout, const std::function<bool(const Advertisement&)>& on_advertisement) = 0;
    // Connects to a device seen while scanning. Null with a reason in `error` on failure.
    virtual std::unique_ptr<GattLink> connect(const std::string& address, std::string* error) = 0;
};

// Lowercase, so addresses from any backend (and older saved settings) compare equal.
std::string normalize_address(std::string address);

}  // namespace djivcam::ble::detail
