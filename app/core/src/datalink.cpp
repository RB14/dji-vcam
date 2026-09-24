#include "djivcam/datalink.h"

#include <array>

namespace djivcam::datalink {
namespace {

constexpr std::size_t kStatusFrameLen = 34;
// Without video for this long, the ACK falls back to the camera's own video cursor.
constexpr std::chrono::milliseconds kVideoStall{300};
constexpr std::uint8_t kPairingSetId = 0x07;
constexpr std::uint8_t kPairingCmdId = 0x45;
constexpr std::uint16_t kPairingSeq = 0x8092;

// Window 100, MTU 1472 (c0 05) and the rest as DJI Mimo sends it; prefixed by our base sequence.
constexpr std::array<std::uint8_t, 38> kHandshakeTail = {
    0x64, 0x00, 0x64, 0x00, 0xc0, 0x05, 0x14, 0x00, 0x00, 0x64, 0x00, 0x00, 0x01,
    0x90, 0x01, 0xc0, 0x05, 0x14, 0x00, 0x00, 0x64, 0x00, 0x14, 0x00, 0x64, 0x00,
    0xc0, 0x05, 0x14, 0x00, 0x00, 0x64, 0x00, 0x01, 0x01, 0x04, 0x01, 0x02};

void put_le16(Bytes& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}

std::uint16_t read_le16(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8));
}

}  // namespace

Bytes header(PacketType type, std::size_t payload_len, std::uint16_t session_id, std::uint16_t seq) {
    Bytes out;
    out.reserve(kHeaderLen);
    put_le16(out, static_cast<std::uint16_t>(0x8000 | ((kHeaderLen + payload_len) & 0x3FFF)));
    put_le16(out, session_id);
    put_le16(out, seq);
    out.push_back(static_cast<std::uint8_t>(type));
    std::uint8_t x = 0;
    for (std::uint8_t byte : out) {
        x ^= byte;
    }
    out.push_back(x);
    return out;
}

Bytes routing_header(std::uint16_t seq, std::uint8_t cmd_counter) {
    Bytes out;
    out.reserve(12);
    put_le16(out, static_cast<std::uint16_t>(seq - 8));
    put_le16(out, seq);
    out.insert(out.end(), 4, 0x00);
    out.push_back(cmd_counter);
    out.push_back(0x01);
    out.insert(out.end(), 2, 0x00);
    return out;
}

bool seq_ahead(std::uint16_t newer, std::uint16_t older) {
    const auto diff = static_cast<std::uint16_t>(newer - older);
    return diff != 0 && diff < 0x8000;
}

bool seq_near(std::uint16_t a, std::uint16_t b, std::uint16_t window) {
    const auto diff = static_cast<std::uint16_t>(a - b);
    return diff < window || diff > static_cast<std::uint16_t>(0x10000 - window);
}

Link::Link(net::UdpSocket socket, std::string camera_ip, std::uint16_t port)
    : socket_(std::move(socket)), camera_ip_(std::move(camera_ip)), port_(port) {
    session_id_ = static_cast<std::uint16_t>(std::uniform_int_distribution<int>(0x1000, 0xFFFE)(rng_));
    base_ = static_cast<std::uint16_t>(std::uniform_int_distribution<int>(0x1000, 0xF000)(rng_) & 0xFFF8);
    camera_channel_ = base_;
}

void Link::send(PacketType type, std::span<const std::uint8_t> payload, std::optional<std::uint16_t> seq) {
    Bytes packet = header(type, payload.size(), session_id_, seq.value_or(seq_));
    packet.insert(packet.end(), payload.begin(), payload.end());
    socket_.send_to(camera_ip_, port_, packet);
    if (!seq) {
        seq_ = static_cast<std::uint16_t>(seq_ + 8);
    }
}

void Link::send_frame(const duml::Frame& frame) {
    ++cmd_counter_;
    Bytes body = routing_header(seq_, cmd_counter_);
    const Bytes encoded = frame.encode();
    body.insert(body.end(), encoded.begin(), encoded.end());
    send(PacketType::Command, body);
}

std::uint16_t Link::send_duml(std::uint8_t receiver, std::uint8_t cmd_set, std::uint8_t cmd_id, Bytes payload,
                              std::uint8_t flags) {
    const std::uint16_t seq = duml_seq_++;
    send_frame(duml::Frame{duml::kAddrApp, receiver, seq, flags, cmd_set, cmd_id, std::move(payload)});
    return seq;
}

void Link::send_ack() {
    // While video flows, ack what we received; when it stalls, fall back to the camera's own
    // video cursor from its status frames so a stuck window can always recover.
    const bool video_fresh = std::chrono::steady_clock::now() - last_video_time_ < kVideoStall;
    const std::uint16_t video = video_ack_ && video_fresh ? *video_ack_ : video_cursor_;
    Bytes payload;
    for (std::uint16_t cursor : {video, download_cursor_, base_}) {
        put_le16(payload, cursor);
        put_le16(payload, cursor);
        payload.insert(payload.end(), 4, 0x00);
    }
    payload.insert(payload.end(), 2, 0x00);
    send(PacketType::Ack, payload, std::uint16_t{0});
}

std::optional<Datagram> Link::receive(std::chrono::milliseconds timeout) {
    std::array<std::uint8_t, 65536> buffer;
    const auto size = socket_.receive(buffer, timeout);
    if (!size || *size < kHeaderLen) {
        return std::nullopt;
    }
    const std::span<const std::uint8_t> raw(buffer.data(), *size);
    Datagram datagram{static_cast<PacketType>(raw[6]), read_le16(raw, 4), Bytes(raw.begin(), raw.end())};
    if (raw.size() >= 10) {
        if (const std::uint16_t channel = read_le16(raw, 8)) {
            camera_channel_ = channel;
        }
    }
    if (datagram.type == PacketType::Status && raw.size() == kStatusFrameLen) {
        video_cursor_ = read_le16(raw, 10);
        download_cursor_ = read_le16(raw, 18);
    }
    if (datagram.type == PacketType::Video) {
        last_video_time_ = std::chrono::steady_clock::now();
    }
    return datagram;
}

std::vector<Datagram> Link::receive_all(std::chrono::milliseconds window) {
    std::vector<Datagram> out;
    const auto deadline = std::chrono::steady_clock::now() + window;
    while (true) {
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            return out;
        }
        if (auto datagram = receive(remaining)) {
            out.push_back(std::move(*datagram));
        }
    }
}

std::vector<duml::Frame> Link::duml_frames(const Datagram& datagram) {
    switch (datagram.type) {
    case PacketType::Status:
    case PacketType::Reliable:
    case PacketType::Command:
        return parser_.feed(datagram.payload());
    default:
        return {};
    }
}

bool Link::poke(const std::string& identifier, const std::string& token) {
    Bytes payload = duml::pack_string(identifier);
    const Bytes token_bytes = duml::pack_string(token);
    payload.insert(payload.end(), token_bytes.begin(), token_bytes.end());
    const duml::Frame frame{duml::kAddrApp,  duml::kAddrWifi, kPairingSeq, duml::kFlagRequest,
                            kPairingSetId,   kPairingCmdId,   std::move(payload)};
    using namespace std::chrono_literals;
    return net::tcp_send_once(camera_ip_, kPokePort, frame.encode(), 1200ms, 400ms);
}

std::optional<Datagram> Link::handshake(int attempts) {
    Bytes payload;
    put_le16(payload, base_);
    payload.insert(payload.end(), kHandshakeTail.begin(), kHandshakeTail.end());
    using namespace std::chrono_literals;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        send(PacketType::Handshake, payload, std::uint16_t{0});
        for (auto& datagram : receive_all(350ms)) {
            if (datagram.type == PacketType::Handshake) {
                return std::move(datagram);
            }
        }
    }
    return std::nullopt;
}

void Link::settle(int rounds) {
    using namespace std::chrono_literals;
    for (int round = 0; round < rounds; ++round) {
        receive_all(400ms);
        send_ack();
    }
    seq_ = static_cast<std::uint16_t>(camera_channel_ + 8);
}

}  // namespace djivcam::datalink
