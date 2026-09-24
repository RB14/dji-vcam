// DJI camera Wi-Fi datalink: the UDP 9004 transport DJI Mimo uses on the camera's own AP.
//
// Every datagram starts with an 8-byte header (all little-endian):
//   [0:2] 0x8000 | total length   [2:4] session id   [4:6] seq   [6] packet type   [7] XOR of [0:7]
//
// Mirrors tools/datalink.py (layouts from osmosis, verified on the Action 5 Pro);
// see docs/protocol-notes.md section (b).
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <vector>

#include "osmolink/duml.h"
#include "osmolink/net.h"

namespace osmolink::datalink {

using duml::Bytes;

inline constexpr const char* kCameraIp = "192.168.2.1";
inline constexpr std::uint16_t kDatalinkPort = 9004;
inline constexpr std::uint16_t kPokePort = 7001;
inline constexpr std::size_t kHeaderLen = 8;

enum class PacketType : std::uint8_t {
    Handshake = 0x00,
    Status = 0x01,  // camera status / DUML pushes (34-byte window-status frames among them)
    Video = 0x02,
    Reliable = 0x03,
    Ack = 0x04,     // app -> camera window ACK
    Command = 0x05, // 12-byte routing header + DUML
};

Bytes header(PacketType type, std::size_t payload_len, std::uint16_t session_id, std::uint16_t seq);
// [ack = our previous seq][our seq][0 x4][counter][01][00 00]
Bytes routing_header(std::uint16_t seq, std::uint8_t cmd_counter);
// True if 16-bit sequence `newer` is ahead of `older` (with wrap-around).
bool seq_ahead(std::uint16_t newer, std::uint16_t older);
// True if 16-bit sequences `a` and `b` are within `window` of each other (with wrap-around).
bool seq_near(std::uint16_t a, std::uint16_t b, std::uint16_t window = 1024);

struct Datagram {
    PacketType type;
    std::uint16_t seq;
    Bytes raw;

    std::span<const std::uint8_t> payload() const {
        return std::span<const std::uint8_t>(raw).subspan(kHeaderLen);
    }
};

class Link {
public:
    Link(net::UdpSocket socket, std::string camera_ip, std::uint16_t port);

    // TCP 7001 "poke" with a SetPairingPIN frame; arms the datalink on 9004 bodies.
    bool poke(const std::string& identifier, const std::string& token);
    std::optional<Datagram> handshake(int attempts = 20);
    // Drains the camera's first packets (acking each batch), then syncs our seq to its channel.
    void settle(int rounds = 5);

    void send_frame(const duml::Frame& frame);
    void send_duml(std::uint8_t receiver, std::uint8_t cmd_set, std::uint8_t cmd_id, Bytes payload,
                   std::uint8_t flags = duml::kFlagRequest);
    // Window ACK: [start][end][u32 0] for video, download and control, plus u16 0. Seq 0.
    void send_ack();
    // Video position to acknowledge: the last contiguous datagram received. Acknowledging only
    // contiguous data tells the camera about gaps so it can re-send them.
    void set_video_ack(std::optional<std::uint16_t> seq) { video_ack_ = seq; }

    // Receives one datagram, waiting up to `timeout`.
    std::optional<Datagram> receive(std::chrono::milliseconds timeout);
    // Everything that arrives within `window`.
    std::vector<Datagram> receive_all(std::chrono::milliseconds window);
    std::vector<duml::Frame> duml_frames(const Datagram& datagram);

    std::uint16_t session_id() const { return session_id_; }
    std::uint16_t base() const { return base_; }
    std::uint16_t camera_channel() const { return camera_channel_; }

private:
    void send(PacketType type, std::span<const std::uint8_t> payload, std::optional<std::uint16_t> seq = {});

    net::UdpSocket socket_;
    std::string camera_ip_;
    std::uint16_t port_;
    std::mt19937 rng_{std::random_device{}()};
    std::uint16_t session_id_;
    std::uint16_t base_;
    std::uint16_t seq_ = 0;
    std::uint16_t camera_channel_;
    std::uint8_t cmd_counter_ = 0;
    std::uint16_t duml_seq_ = 0xA000;
    std::uint16_t video_cursor_ = 0;
    std::uint16_t download_cursor_ = 0;
    std::optional<std::uint16_t> video_ack_;
    std::chrono::steady_clock::time_point last_video_time_{};
    duml::StreamParser parser_;
};

}  // namespace osmolink::datalink
