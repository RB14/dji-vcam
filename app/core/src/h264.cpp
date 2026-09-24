#include "osmolink/h264.h"

#include <algorithm>
#include <array>

namespace osmolink::h264 {
namespace {

constexpr std::array<std::uint8_t, 3> kStartCode = {0x00, 0x00, 0x01};
constexpr std::array<std::uint8_t, 4> kLongStartCode = {0x00, 0x00, 0x00, 0x01};
constexpr std::uint8_t kDjiUnitHeader = 0xFF;

// Position of the next 3-byte start code at or after `from`, or data.size().
std::size_t find_start_code(std::span<const std::uint8_t> data, std::size_t from) {
    const auto it = std::search(data.begin() + static_cast<std::ptrdiff_t>(from), data.end(), kStartCode.begin(),
                                kStartCode.end());
    return static_cast<std::size_t>(it - data.begin());
}

}  // namespace

void AccessUnitAssembler::push(std::span<const std::uint8_t> data) {
    pending_.insert(pending_.end(), data.begin(), data.end());
    const std::span<const std::uint8_t> buffer(pending_);

    std::size_t start = find_start_code(buffer, 0);
    // Without a complete start code, keep the last two bytes: they may begin one ("00 00" | "01").
    std::size_t consumed = start < buffer.size() ? start : buffer.size() - std::min<std::size_t>(buffer.size(), 2);
    while (start < buffer.size()) {
        const std::size_t nal_begin = start + kStartCode.size();
        const std::size_t next = find_start_code(buffer, nal_begin);
        if (next >= buffer.size()) {
            break;  // last NAL may still be incomplete; keep it for the next push
        }
        std::size_t nal_end = next;
        while (nal_end > nal_begin && buffer[nal_end - 1] == 0x00) {
            --nal_end;  // trailing zeros belong to the next (4-byte) start code
        }
        if (nal_end > nal_begin) {
            on_nal(buffer.subspan(nal_begin, nal_end - nal_begin));
        }
        start = next;
        consumed = next;
    }
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(consumed));
}

void AccessUnitAssembler::on_nal(std::span<const std::uint8_t> nal) {
    if (nal[0] == kDjiUnitHeader) {
        return;
    }
    const std::uint8_t type = nal[0] & 0x1F;
    if (type == kNalAud) {
        flush();
    }
    current_.data.insert(current_.data.end(), kLongStartCode.begin(), kLongStartCode.end());
    current_.data.insert(current_.data.end(), nal.begin(), nal.end());
    current_.keyframe = current_.keyframe || type == kNalIdr;
}

void AccessUnitAssembler::flush() {
    if (!current_.data.empty()) {
        callback_(std::move(current_));
    }
    current_ = AccessUnit{};
}

void AccessUnitAssembler::reset() {
    pending_.clear();
    current_ = AccessUnit{};
}

}  // namespace osmolink::h264
