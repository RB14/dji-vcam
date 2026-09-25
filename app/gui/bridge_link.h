// The ESP32-S3 USB Wi-Fi bridge (firmware/usb-wifi-bridge), reached through its USB serial
// console: finds it and makes sure it joins the camera's access point.
#pragma once

#include <QString>

#include <optional>
#include <vector>

#include "djivcam/wifi_channel.h"

class BridgeLink {
public:
    // Serial port of a connected bridge (USB VID 0x303A, PID 0x4001), if any.
    static std::optional<QString> findPort();

    // Gives the bridge the camera AP credentials unless it already has them. Returns false when
    // no bridge is connected or it does not answer. `error` receives a human-readable reason.
    static bool ensureCredentials(const QString& ssid, const QString& password, QString* error);

    // The 2.4 GHz networks the bridge hears (its `scan` command, a few seconds). Blocking: call it
    // off the UI thread, and not while video streams (the bridge leaves the camera's channel
    // briefly to scan).
    static std::optional<std::vector<djivcam::wifi::Network>> scan(QString* error);
};
