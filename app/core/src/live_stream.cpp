#include "djivcam/live_stream.h"

#include <cstdio>
#include <stdexcept>

namespace djivcam::live {
namespace {

constexpr std::size_t kMaxSsid = 32;
constexpr std::size_t kMaxPassword = 63;
// The start command's payload stays well inside one DUML frame over Bluetooth (Mimo's: 141 bytes).
constexpr std::size_t kMaxStartPayload = 400;

// The camera's JSON has its slashes escaped ("rtmp:\/\/host"), as DJI Mimo sends it.
std::string escape_json(std::string_view text) {
    std::string out;
    for (char c : text) {
        if (c == '"' || c == '\\' || c == '/') {
            out += '\\';
        }
        out += c;
    }
    return out;
}

}  // namespace

Command live_mode() { return {kAddrLive, 0x02, 0xE1, {0x1A}}; }

Command video_mode() { return {kAddrLive, 0x02, 0xE1, {0x01}}; }

Command scan_networks() { return {kAddrWifiScan, 0x07, 0xAB, {}}; }

Command join_network(std::string_view ssid, std::string_view password) {
    if (ssid.empty() || ssid.size() > kMaxSsid || password.size() > kMaxPassword) {
        throw std::invalid_argument("network name must be 1-32 bytes and the password up to 63");
    }
    Bytes payload;
    for (std::string_view text : {ssid, password}) {
        payload.push_back(static_cast<std::uint8_t>(text.size()));
        payload.insert(payload.end(), text.begin(), text.end());
    }
    return {duml::kAddrWifi, 0x07, 0x47, std::move(payload)};
}

Command start_stream(const StreamSettings& settings) {
    // 01 8a 00 <resolution> <kbit/s u16 LE> fe 01 00 00 00 00 7f 00, then JSON (protocol-notes 3.13).
    Bytes payload = {0x01, 0x8A, 0x00, static_cast<std::uint8_t>(settings.resolution),
                     static_cast<std::uint8_t>(settings.kbps & 0xFF), static_cast<std::uint8_t>(settings.kbps >> 8),
                     0xFE, 0x01, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x00};
    const std::string json = "{\"rtmpAddress\":\"" + escape_json(settings.url) +
                             "\",\"watermark\":0,\"codec\":\"\",\"EnhancedRTMP\":false,\"supportStopLive\":false}";
    payload.insert(payload.end(), json.begin(), json.end());
    if (settings.url.empty() || payload.size() > kMaxStartPayload) {
        throw std::invalid_argument("the RTMP address is empty or too long");
    }
    return {kAddrLive, 0x08, 0x78, std::move(payload)};
}

Command stop_stream() { return {kAddrLive, 0x02, 0x8E, {0x01, 0x01, 0x1A, 0x00, 0x01, 0x02}}; }

Command read_stream_settings() { return {kAddrLive, 0x08, 0x79, {0x01}}; }

Command keep_alive() { return {duml::kAddrSession, 0x00, 0x2B, {0x04, 0x00}}; }

Command wifi_mac() { return {duml::kAddrWifi, 0x07, 0x0C, {}}; }

bool accepted(const duml::Frame& reply) { return !reply.payload.empty() && reply.payload[0] == 0x00; }

std::optional<StreamSettings> parse_stream_settings(std::span<const std::uint8_t> payload) {
    // 00 01 8a 00 <resolution> <kbit/s u16 LE> 00 01 00 00 00 00 7f 00 <address, NUL-padded>
    constexpr std::size_t kAddress = 15;
    if (payload.size() < kAddress || payload[0] != 0x00) {
        return std::nullopt;
    }
    const auto resolution = static_cast<Resolution>(payload[4]);
    if (resolution != Resolution::P720 && resolution != Resolution::P1080) {
        return std::nullopt;
    }
    StreamSettings settings;
    settings.resolution = resolution;
    settings.kbps = static_cast<std::uint16_t>(payload[5] | (payload[6] << 8));
    for (std::size_t i = kAddress; i < payload.size() && payload[i] != 0; ++i) {
        settings.url += static_cast<char>(payload[i]);
    }
    return settings;
}

std::vector<Network> parse_network_list(std::span<const std::uint8_t> payload) {
    // A 4-byte header, then per network [length incl. itself] 01 01 <band> <flag> 00 <name>.
    constexpr std::size_t kHeader = 4;
    constexpr std::size_t kEntryHead = 6;
    std::vector<Network> networks;
    for (std::size_t i = kHeader; i < payload.size();) {
        const std::size_t length = payload[i];
        if (length < kEntryHead || i + length > payload.size()) {
            break;  // not an entry: the rest cannot be trusted either
        }
        if (length > kEntryHead) {
            Network network;
            network.five_ghz = payload[i + 3] == 0x02;
            network.flag = payload[i + 4];
            network.ssid.assign(payload.begin() + static_cast<std::ptrdiff_t>(i + kEntryHead),
                                payload.begin() + static_cast<std::ptrdiff_t>(i + length));
            networks.push_back(std::move(network));
        }
        i += length;
    }
    return networks;
}

std::optional<std::string> parse_mac(std::span<const std::uint8_t> payload) {
    if (payload.size() < 7 || payload[0] != 0x00) {
        return std::nullopt;
    }
    char text[18];
    std::snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x", payload[1], payload[2], payload[3], payload[4],
                  payload[5], payload[6]);
    return std::string(text);
}

}  // namespace djivcam::live
