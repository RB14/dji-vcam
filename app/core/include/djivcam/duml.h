// DJI DUML framing (SOF 0x55), as spoken over BLE, TCP 7001 and the UDP 9004 datalink.
//
// Frame layout:
//   0     SOF 0x55
//   1..2  u16-LE: bits[9:0] total length (13 + payload), bits[15:10] version (1)
//   3     CRC8 over bytes [0:3]
//   4     sender   (id << 5) | type
//   5     receiver (id << 5) | type
//   6..7  sequence / message id (the camera echoes it verbatim)
//   8     flags (cmd_type << 5) | encrypt: 0x40 request, 0xC0 response, 0x00 push
//   9     command set
//   10    command id
//   11..  payload
//   -2..  CRC16-LE over everything before it
//
// Mirrors tools/duml.py; see docs/protocol-notes.md.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace djivcam::duml {

using Bytes = std::vector<std::uint8_t>;

inline constexpr std::uint8_t kSof = 0x55;
inline constexpr std::size_t kOverhead = 13;

inline constexpr std::uint8_t kFlagPush = 0x00;
inline constexpr std::uint8_t kFlagRequest = 0x40;
inline constexpr std::uint8_t kFlagWrite = 0x80;
inline constexpr std::uint8_t kFlagResponse = 0xC0;

// Endpoint addresses, (id << 5) | type.
inline constexpr std::uint8_t kAddrCamera = 0x01;
inline constexpr std::uint8_t kAddrApp = 0x02;
inline constexpr std::uint8_t kAddrWifi = 0x07;
inline constexpr std::uint8_t kAddrDm368First = 0x28;
inline constexpr std::uint8_t kAddrDm368Second = 0x48;
inline constexpr std::uint8_t kAddrSession = 0xF0;
inline constexpr std::uint8_t kAddrSystem = 0x1C;

std::uint8_t crc8(std::span<const std::uint8_t> data);
std::uint16_t crc16(std::span<const std::uint8_t> data);

// [len:u8][utf8 bytes]
Bytes pack_string(std::string_view text);

struct Frame {
    std::uint8_t sender = kAddrApp;
    std::uint8_t receiver = 0;
    std::uint16_t seq = 0;
    std::uint8_t flags = kFlagRequest;
    std::uint8_t cmd_set = 0;
    std::uint8_t cmd_id = 0;
    Bytes payload;

    bool is_request() const { return (flags & 0xE0) == kFlagRequest; }
    Bytes encode() const;
    // The response to this request: addresses swapped, same sequence.
    Frame reply(Bytes reply_payload) const;
    std::string describe() const;
};

// Decodes exactly one complete, CRC-valid frame.
std::optional<Frame> decode(std::span<const std::uint8_t> raw);

// Reassembles frames from a byte stream (BLE notifications, TCP, UDP payloads), skipping junk.
// Frames starting with 0xAA (DJI R-SDK protocol, also seen on the camera's notify
// characteristic) are skipped whole.
class StreamParser {
public:
    std::vector<Frame> feed(std::span<const std::uint8_t> data);

private:
    Bytes buffer_;
};

}  // namespace djivcam::duml
