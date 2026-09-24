// Splits the live-view H.264 byte stream into access units (one per video frame).
//
// The camera's video datagrams, stripped of their 12-byte sub-header and concatenated, form an
// Annex-B stream: AUD, SEI, [SPS, PPS,] slices per frame, plus a DJI-proprietary unit with header
// byte 0xFF after each AUD, which is dropped here (decoders would reject it).
#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace djivcam::h264 {

using Bytes = std::vector<std::uint8_t>;

inline constexpr std::uint8_t kNalSlice = 1;
inline constexpr std::uint8_t kNalIdr = 5;
inline constexpr std::uint8_t kNalSps = 7;
inline constexpr std::uint8_t kNalAud = 9;

struct AccessUnit {
    Bytes data;  // Annex-B, 4-byte start codes
    bool keyframe = false;
};

class AccessUnitAssembler {
public:
    using Callback = std::function<void(AccessUnit&&)>;

    explicit AccessUnitAssembler(Callback callback) : callback_(std::move(callback)) {}

    void push(std::span<const std::uint8_t> data);
    // Emits the access unit collected so far (e.g. when the transport signals end of frame).
    // TODO: drive this from the video sub-header's end-of-frame marker once confirmed, which
    // would save up to one frame of latency compared to waiting for the next AUD.
    void flush();
    // Drops all buffered data (e.g. after a stream restart).
    void reset();

private:
    void on_nal(std::span<const std::uint8_t> nal);

    Callback callback_;
    Bytes pending_;  // stream bytes not yet split into NAL units
    AccessUnit current_;
};

}  // namespace djivcam::h264
