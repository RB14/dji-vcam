#include "djivcam/wifi_channel.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>

namespace djivcam::wifi {
namespace {

constexpr int kCandidates[] = {1, 6, 11};
// Share of a 20 MHz network's power that falls into a channel 0, 1, 2, 3, 4 channels (5 MHz each)
// away; nothing from 5 on.
constexpr double kOverlap[] = {1.0, 0.7, 0.4, 0.15, 0.05};
// Move only if the current channel has more than this much more. A scan shows how loud the
// neighbours are, not how busy: on 2026-09-25 channel 10 (off the 1/6/11 grid, so it shares the
// air of two grid channels' networks) starved the live view while carrying only 1.3 times
// channel 1's interference, and channel 1 was clean.
constexpr double kMinGainOffGrid = 1.25;
// From one grid channel to another only for a clear gain (3 dB): one scan's levels vary by a few
// dB, and the camera went 6 -> 11 -> 6 on two app starts with 1.25.
constexpr double kMinGainOnGrid = 2.0;

bool on_grid(int channel) { return std::find(std::begin(kCandidates), std::end(kCandidates), channel) != std::end(kCandidates); }

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
    const double min_gain = on_grid(current_channel) ? kMinGainOnGrid : kMinGainOffGrid;
    if (current_channel > 0 && !(interference(others, current_channel) > min_gain * interference(others, best))) {
        return 0;  // not worth restarting the camera's Wi-Fi (also when the air is empty everywhere)
    }
    return best;
}

}  // namespace djivcam::wifi
