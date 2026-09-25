#include "djivcam/wifi_channel.h"

#include <cmath>
#include <cstdlib>

namespace djivcam::wifi {
namespace {

constexpr int kCandidates[] = {1, 6, 11};
// Share of a 20 MHz network's power that falls into a channel 0, 1, 2, 3, 4 channels (5 MHz each)
// away; nothing from 5 on.
constexpr double kOverlap[] = {1.0, 0.7, 0.4, 0.15, 0.05};
// Move only if the current channel has at least this much more. A scan shows how loud the
// neighbours are, not how busy: on 2026-09-25 channel 10 starved the live view while carrying only
// 1.3 times channel 1's interference, and channel 1 was clean.
constexpr double kMinGain = 1.25;

}  // namespace

double interference(const std::vector<Network>& networks, int channel) {
    double total = 0.0;
    for (const Network& network : networks) {
        const int distance = std::abs(network.channel - channel);
        if (network.channel > 0 && distance < static_cast<int>(std::size(kOverlap))) {
            total += std::pow(10.0, network.rssi / 10.0) * kOverlap[distance];
        }
    }
    return total;
}

int quietest_channel(const std::vector<Network>& networks, const std::string& camera_ssid, int current_channel) {
    std::vector<Network> others;
    for (const Network& network : networks) {
        if (network.ssid != camera_ssid) {
            others.push_back(network);
        }
    }
    int best = kCandidates[0];
    for (int channel : kCandidates) {
        if (interference(others, channel) < interference(others, best)) {
            best = channel;
        }
    }
    if (current_channel == best) {
        return 0;
    }
    if (current_channel > 0 && !(interference(others, current_channel) > kMinGain * interference(others, best))) {
        return 0;  // not worth restarting the camera's Wi-Fi (also when the air is empty everywhere)
    }
    return best;
}

}  // namespace djivcam::wifi
