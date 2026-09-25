// Choosing a quiet 2.4 GHz channel for the camera's access point.
//
// The camera sits on one channel for good (it moves when told: 0x07/0x2B, see
// docs/protocol-notes.md 3.12), and a busy neighbouring network on or next to it can starve the
// live view. The ESP32 bridge scans the networks around it; this picks the channel among 1, 6 and
// 11 (they do not overlap, and every country allows them) where they interfere least.
#pragma once

#include <string>
#include <vector>

namespace djivcam::wifi {

// A network heard in a scan.
struct Network {
    int channel = 0;  // 1..14
    int rssi = 0;     // dBm
    std::string ssid;
};

// Interference on `channel`: the power the networks put into it (mW), each weighted by how much
// its 20 MHz channel overlaps (fully on the same channel, not at all 5 or more channels away).
double interference(const std::vector<Network>& networks, int channel);

// The channel to move the camera to, or 0 to stay: the quietest of 1, 6 and 11, when the camera's
// current channel (0 if unknown) carries at least 1.25 times its interference. The camera's own
// network (`camera_ssid`) is not counted.
int quietest_channel(const std::vector<Network>& networks, const std::string& camera_ssid, int current_channel);

}  // namespace djivcam::wifi
