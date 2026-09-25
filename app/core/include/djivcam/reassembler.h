// Orders the camera's video datagrams and waits briefly for missing ones.
//
// The video channel is a sliding window acknowledged by the app. Acknowledging only the last
// contiguous datagram tells the camera about a gap so it can re-send it; datagrams that arrive
// after a gap are held until the gap is filled. If a gap is not filled within `gap_timeout`, it
// is skipped (the data is lost) so the stream never stalls.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace djivcam {

struct ReassemblerStats {
    std::uint64_t delivered = 0;
    std::uint64_t recovered = 0;   // datagrams that filled a gap (late or re-sent)
    std::uint64_t skipped = 0;     // datagrams given up on after gap_timeout
    std::uint64_t duplicates = 0;  // datagrams already delivered or skipped
    std::uint64_t restarts = 0;    // far sequence jumps (the camera restarted its stream)
};

class VideoReassembler {
public:
    using Clock = std::chrono::steady_clock;
    using Deliver = std::function<void(std::span<const std::uint8_t>)>;
    // Called with the number of datagrams given up on, before the data after the gap is delivered
    // (the camera never re-sends them: what follows is damaged until the next keyframe).
    using OnSkip = std::function<void(std::uint64_t skipped)>;

    static constexpr std::uint16_t kSeqStep = 8;        // the camera advances its seq by 8
    static constexpr std::uint16_t kMaxWindow = 1024;   // farther jumps are stream restarts

    VideoReassembler(Deliver deliver, std::chrono::milliseconds gap_timeout, OnSkip on_skip = {});

    void push(std::uint16_t seq, std::span<const std::uint8_t> payload, Clock::time_point now);
    // Gives up on gaps older than gap_timeout.
    void poll(Clock::time_point now);
    void reset();

    // Last contiguous datagram delivered: the value to acknowledge.
    std::optional<std::uint16_t> ack_seq() const;
    const ReassemblerStats& stats() const { return stats_; }

private:
    void deliver(std::span<const std::uint8_t> payload);
    // Delivers held datagrams that have become contiguous.
    void drain(Clock::time_point now);

    Deliver deliver_;
    std::chrono::milliseconds gap_timeout_;
    OnSkip on_skip_;
    std::optional<std::uint16_t> expected_;
    // Datagrams received ahead of a gap, keyed by sequence number.
    std::map<std::uint16_t, std::vector<std::uint8_t>> held_;
    std::optional<Clock::time_point> gap_since_;
    ReassemblerStats stats_;
};

}  // namespace djivcam
