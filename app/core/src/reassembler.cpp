#include "djivcam/reassembler.h"

#include <algorithm>

namespace djivcam {

VideoReassembler::VideoReassembler(Deliver deliver, std::chrono::milliseconds gap_timeout, OnSkip on_skip)
    : deliver_(std::move(deliver)), gap_timeout_(gap_timeout), on_skip_(std::move(on_skip)) {}

void VideoReassembler::reset() {
    expected_.reset();
    held_.clear();
    gap_since_.reset();
}

void VideoReassembler::deliver(std::span<const std::uint8_t> payload) {
    ++stats_.delivered;
    deliver_(payload);
}

void VideoReassembler::push(std::uint16_t seq, std::span<const std::uint8_t> payload, Clock::time_point now) {
    if (!expected_) {
        deliver(payload);
        expected_ = static_cast<std::uint16_t>(seq + kSeqStep);
        return;
    }
    const auto ahead = static_cast<std::uint16_t>(seq - *expected_);
    const auto behind = static_cast<std::uint16_t>(*expected_ - seq);
    if (ahead == 0) {
        if (gap_since_) {
            ++stats_.recovered;  // this datagram filled a gap
        }
        deliver(payload);
        expected_ = static_cast<std::uint16_t>(seq + kSeqStep);
        drain(now);
    } else if (ahead < kMaxWindow) {
        if (held_.emplace(seq, std::vector<std::uint8_t>(payload.begin(), payload.end())).second && !gap_since_) {
            gap_since_ = now;
        }
    } else if (behind <= kMaxWindow) {
        ++stats_.duplicates;  // already delivered or skipped
    } else {
        ++stats_.restarts;  // far jump: the camera restarted its video stream
        reset();
        push(seq, payload, now);
    }
}

void VideoReassembler::drain(Clock::time_point now) {
    for (auto it = held_.find(*expected_); it != held_.end(); it = held_.find(*expected_)) {
        deliver(it->second);
        held_.erase(it);
        expected_ = static_cast<std::uint16_t>(*expected_ + kSeqStep);
    }
    // Anything still held sits behind a new gap, which starts now.
    gap_since_ = held_.empty() ? std::nullopt : std::optional(now);
}

void VideoReassembler::poll(Clock::time_point now) {
    if (!gap_since_ || held_.empty() || now - *gap_since_ < gap_timeout_) {
        return;
    }
    // Give up on the gap: skip ahead to the nearest held datagram.
    std::uint16_t nearest = kMaxWindow;
    for (const auto& [seq, payload] : held_) {
        nearest = std::min(nearest, static_cast<std::uint16_t>(seq - *expected_));
    }
    stats_.skipped += nearest / kSeqStep;
    if (on_skip_) {
        on_skip_(nearest / kSeqStep);
    }
    expected_ = static_cast<std::uint16_t>(*expected_ + nearest);
    drain(now);
}

std::optional<std::uint16_t> VideoReassembler::ack_seq() const {
    if (!expected_) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(*expected_ - kSeqStep);
}

}  // namespace djivcam
