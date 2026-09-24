#include "djivcam/duml.h"

#include <array>
#include <cstdio>

namespace djivcam::duml {
namespace {

constexpr std::uint16_t kVersionBits = 0x0400;
constexpr std::size_t kHeaderLen = 11;
constexpr std::uint16_t kMaxLength = 0x3FF;

constexpr std::array<std::uint8_t, 256> make_crc8_table() {
    std::array<std::uint8_t, 256> table{};
    for (unsigned byte = 0; byte < 256; ++byte) {
        unsigned crc = byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1) ? (crc >> 1) ^ 0x8C : crc >> 1;
        }
        table[byte] = static_cast<std::uint8_t>(crc);
    }
    return table;
}

constexpr std::array<std::uint16_t, 256> make_crc16_table() {
    std::array<std::uint16_t, 256> table{};
    for (unsigned byte = 0; byte < 256; ++byte) {
        unsigned crc = byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : crc >> 1;
        }
        table[byte] = static_cast<std::uint16_t>(crc);
    }
    return table;
}

constexpr auto kCrc8Table = make_crc8_table();
constexpr auto kCrc16Table = make_crc16_table();

std::uint16_t read_le16(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8));
}

}  // namespace

std::uint8_t crc8(std::span<const std::uint8_t> data) {
    std::uint8_t crc = 0x77;
    for (std::uint8_t byte : data) {
        crc = kCrc8Table[crc ^ byte];
    }
    return crc;
}

std::uint16_t crc16(std::span<const std::uint8_t> data) {
    std::uint16_t crc = 0x3692;
    for (std::uint8_t byte : data) {
        crc = static_cast<std::uint16_t>((crc >> 8) ^ kCrc16Table[(crc ^ byte) & 0xFF]);
    }
    return crc;
}

Bytes pack_string(std::string_view text) {
    Bytes out;
    out.reserve(text.size() + 1);
    out.push_back(static_cast<std::uint8_t>(text.size()));
    out.insert(out.end(), text.begin(), text.end());
    return out;
}

Bytes Frame::encode() const {
    const auto total = static_cast<std::uint16_t>(kOverhead + payload.size());
    Bytes out;
    out.reserve(total);
    const std::uint16_t len_ver = kVersionBits | total;
    out.push_back(kSof);
    out.push_back(static_cast<std::uint8_t>(len_ver & 0xFF));
    out.push_back(static_cast<std::uint8_t>(len_ver >> 8));
    out.push_back(crc8(out));
    out.push_back(sender);
    out.push_back(receiver);
    out.push_back(static_cast<std::uint8_t>(seq & 0xFF));
    out.push_back(static_cast<std::uint8_t>(seq >> 8));
    out.push_back(flags);
    out.push_back(cmd_set);
    out.push_back(cmd_id);
    out.insert(out.end(), payload.begin(), payload.end());
    const std::uint16_t crc = crc16(out);
    out.push_back(static_cast<std::uint8_t>(crc & 0xFF));
    out.push_back(static_cast<std::uint8_t>(crc >> 8));
    return out;
}

Frame Frame::reply(Bytes reply_payload) const {
    return Frame{receiver, sender, seq, kFlagResponse, cmd_set, cmd_id, std::move(reply_payload)};
}

std::string Frame::describe() const {
    char head[64];
    std::snprintf(head, sizeof(head), "%02x->%02x f%02x %02x/%02x seq=%04x payload=", sender, receiver,
                  flags, cmd_set, cmd_id, seq);
    std::string out = head;
    for (std::uint8_t byte : payload) {
        char hex[3];
        std::snprintf(hex, sizeof(hex), "%02x", byte);
        out += hex;
    }
    return out;
}

std::optional<Frame> decode(std::span<const std::uint8_t> raw) {
    if (raw.size() < kOverhead || raw[0] != kSof) {
        return std::nullopt;
    }
    const std::size_t total = read_le16(raw, 1) & kMaxLength;
    if (total != raw.size() || crc8(raw.first(3)) != raw[3]) {
        return std::nullopt;
    }
    if (crc16(raw.first(total - 2)) != read_le16(raw, total - 2)) {
        return std::nullopt;
    }
    Frame frame;
    frame.sender = raw[4];
    frame.receiver = raw[5];
    frame.seq = read_le16(raw, 6);
    frame.flags = raw[8];
    frame.cmd_set = raw[9];
    frame.cmd_id = raw[10];
    frame.payload.assign(raw.begin() + kHeaderLen, raw.end() - 2);
    return frame;
}

std::vector<Frame> StreamParser::feed(std::span<const std::uint8_t> data) {
    buffer_.insert(buffer_.end(), data.begin(), data.end());
    std::vector<Frame> frames;
    std::size_t pos = 0;
    while (pos < buffer_.size()) {
        const std::uint8_t sof = buffer_[pos];
        if (sof != kSof && sof != 0xAA) {
            ++pos;
            continue;
        }
        if (buffer_.size() - pos < 4) {
            break;
        }
        const std::span<const std::uint8_t> rest(buffer_.data() + pos, buffer_.size() - pos);
        const std::size_t total = read_le16(rest, 1) & kMaxLength;
        const bool bad_header = sof == kSof && (total < kOverhead || crc8(rest.first(3)) != rest[3]);
        if (bad_header || total < 4) {
            ++pos;
            continue;
        }
        if (rest.size() < total) {
            break;
        }
        if (sof == kSof) {
            if (auto frame = decode(rest.first(total))) {
                frames.push_back(std::move(*frame));
            }
        }
        pos += total;
    }
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(pos));
    return frames;
}

}  // namespace djivcam::duml
