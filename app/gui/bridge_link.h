// The ESP32-S3 USB Wi-Fi bridge (firmware/usb-wifi-bridge), reached through its USB serial
// console: finds it and makes sure it joins the camera's access point.
#pragma once

#include <QString>

#include <optional>

class BridgeLink {
public:
    // Serial port of a connected bridge (USB VID 0x303A, PID 0x4001), if any.
    static std::optional<QString> findPort();

    // Gives the bridge the camera AP credentials unless it already has them. Returns false when
    // no bridge is connected or it does not answer. `error` receives a human-readable reason.
    static bool ensureCredentials(const QString& ssid, const QString& password, QString* error);
};
