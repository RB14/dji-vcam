// Bluetooth LE session with a DJI Osmo camera: pairing (one on-camera approval per identifier),
// waking its Wi-Fi AP and reading the AP credentials.
//
// Mirrors tools/dji_ble.py; see docs/protocol-notes.md section (a).
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace djivcam::ble {

inline constexpr std::uint16_t kDjiCompanyId = 0x08AA;
inline constexpr std::uint8_t kModelAction5Pro = 0x15;

struct Camera {
    std::string name;
    std::string address;
    int rssi = 0;
    std::uint8_t model = 0;
};

struct WifiCredentials {
    std::string ssid;
    std::string password;
};

enum class PairResult { AlreadyPaired, Approved, TimedOut, Failed };

class CameraBle {
public:
    using Log = std::function<void(const std::string&)>;

    explicit CameraBle(Log log = {});
    ~CameraBle();
    CameraBle(const CameraBle&) = delete;
    CameraBle& operator=(const CameraBle&) = delete;

    static bool bluetooth_available();

    // Scans until a DJI camera advertises (preferring `address`, if given), `timeout` expires or
    // `stop` is requested. Cameras only advertise while awake.
    std::optional<Camera> find_camera(std::chrono::milliseconds timeout, const std::string& address = {},
                                      std::stop_token stop = {});
    bool connect(const Camera& camera);
    bool connected() const;
    void disconnect();

    // Pairs using `identifier` (32 hex chars, remembered by the camera once approved). On first use
    // the camera shows a prompt with `token`; `on_approval_needed` is called when that happens.
    // A stop request ends the wait for the on-camera approval (Failed).
    PairResult pair(const std::string& identifier, const std::string& token, std::chrono::seconds approval_timeout,
                    const std::function<void()>& on_approval_needed, std::stop_token stop = {});
    // Asks the camera to bring up its Wi-Fi AP and returns the AP credentials.
    std::optional<WifiCredentials> wake_wifi();
    // Keeps the link alive; the camera drops idle BLE links after a few seconds.
    void keepalive();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// A fresh random pairing identifier (32 lowercase hex chars).
std::string make_identifier();

// A device heard while scanning (diagnostics).
struct NearbyDevice {
    std::string name;
    std::string address;
    int rssi = 0;
    std::optional<std::uint8_t> dji_model;  // set for DJI devices
};

// Lists the Bluetooth LE devices advertising nearby during `duration` (empty if Bluetooth is off).
std::vector<NearbyDevice> scan_nearby(std::chrono::milliseconds duration);

}  // namespace djivcam::ble
