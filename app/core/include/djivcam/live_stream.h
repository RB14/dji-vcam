// The camera's Live Streaming mode, in which it joins a Wi-Fi network instead of offering its own
// access point (docs/protocol-notes.md 3.13): command builders and parsers for replies and pushes.
//
// In this mode the low-latency live view works over that network (in 1080p), and the camera can
// also push RTMP to a server. The layouts come from a capture of DJI Mimo's livestream setup on the
// Action 5 Pro (2026-09-26). The sequence that uses them is live::Session.
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "djivcam/camera_protocol.h"

namespace djivcam::live {

using camera::Command;
using duml::Bytes;

inline constexpr std::uint8_t kAddrLive = 0x08;      // the camera's livestream and mode handler
inline constexpr std::uint8_t kAddrWifiScan = 0x1B;  // receives the Wi-Fi scan request
// DJI Mimo's Bluetooth keep-alive while the camera is on another network; without it, or without
// the link, the camera returns to its own access point within seconds.
inline constexpr std::chrono::milliseconds kKeepAliveInterval{2500};

// The livestream's frame size, as the start command codes it. The preview stays 1080p regardless.
enum class Resolution : std::uint8_t { P720 = 0x04, P1080 = 0x0A };

// What the camera pushes over RTMP.
struct StreamSettings {
    Resolution resolution = Resolution::P1080;
    std::uint16_t kbps = 6000;
    std::string url;  // rtmp://host:port/path

    bool operator==(const StreamSettings&) const = default;
};

// A Wi-Fi network in the camera's scan list.
struct Network {
    std::string ssid;
    bool five_ghz = false;
    std::uint8_t flag = 0;  // meaning unknown (protocol-notes.md 3.13)

    bool operator==(const Network&) const = default;
};

// 02/E1 1a: Live Streaming mode ("Preparing to live stream"). The join needs it.
Command live_mode();
// 02/E1 01: back to Video mode; ends the join (the camera returns to its access point).
Command video_mode();
// 07/AB: the camera scans for networks and pushes the list (07/AC) seconds later. Optional.
Command scan_networks();
// 07/47: joins the network; answered 00 00 once joined. Throws std::invalid_argument for a name
// that is empty or longer than 32 bytes, or a password longer than 63.
Command join_network(std::string_view ssid, std::string_view password);
// 08/78: starts pushing RTMP to settings.url and stores the settings. Throws std::invalid_argument
// if the URL does not fit in one Bluetooth frame.
Command start_stream(const StreamSettings& settings);
// 02/8E: stops the livestream; this also ends the join and Live Streaming mode.
Command stop_stream();
// 08/79: reads the stored livestream settings (parse_stream_settings()).
Command read_stream_settings();
// 00/2B 04 00, every kKeepAliveInterval while joined.
Command keep_alive();
// 07/0C: the camera's Wi-Fi MAC (parse_mac()), the same as client and as access point.
Command wifi_mac();

// Whether a reply means success (its first byte is 00).
bool accepted(const duml::Frame& reply);
// The stored settings from a 08/79 reply.
std::optional<StreamSettings> parse_stream_settings(std::span<const std::uint8_t> payload);
// The networks in a 07/AC push; entries that do not parse are skipped.
std::vector<Network> parse_network_list(std::span<const std::uint8_t> payload);
// "58:b8:58:00:00:01" from a 07/0C reply.
std::optional<std::string> parse_mac(std::span<const std::uint8_t> payload);

}  // namespace djivcam::live
