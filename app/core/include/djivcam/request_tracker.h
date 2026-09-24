// Matches the camera's DUML replies to the requests the app sent, and gives up on unanswered ones.
//
// A reply carries the request's sequence number and command, the response flag, and comes from the
// endpoint the request went to. Replies with an empty payload are not answers (real ones start
// with a result code) and are ignored, as are pushes that happen to share a sequence number.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "djivcam/duml.h"

namespace djivcam {

class RequestTracker {
public:
    using Clock = std::chrono::steady_clock;
    // Called with the reply, or with nullopt when the request timed out or the link went down.
    using ReplyCallback = std::function<void(std::optional<duml::Frame>)>;

    void add(const duml::Frame& request, ReplyCallback on_reply, Clock::time_point deadline);
    // True if `frame` answered a pending request (whose callback has then run).
    bool resolve(const duml::Frame& frame);
    // Fails the requests whose deadline has passed.
    void expire(Clock::time_point now);
    // Fails every pending request.
    void fail_all();
    std::size_t pending() const { return pending_.size(); }

private:
    struct Pending {
        std::uint16_t seq;
        std::uint8_t receiver;
        std::uint8_t cmd_set;
        std::uint8_t cmd_id;
        ReplyCallback on_reply;
        Clock::time_point deadline;
    };
    std::vector<Pending> pending_;
};

}  // namespace djivcam
