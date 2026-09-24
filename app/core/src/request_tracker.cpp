#include "djivcam/request_tracker.h"

#include <utility>

namespace djivcam {

void RequestTracker::add(const duml::Frame& request, ReplyCallback on_reply, Clock::time_point deadline) {
    pending_.push_back({request.seq, request.cmd_set, request.cmd_id, std::move(on_reply), deadline});
}

bool RequestTracker::resolve(const duml::Frame& frame) {
    if (frame.is_request() || frame.payload.empty()) {
        return false;
    }
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        if (it->seq == frame.seq && it->cmd_set == frame.cmd_set && it->cmd_id == frame.cmd_id) {
            ReplyCallback on_reply = std::move(it->on_reply);
            pending_.erase(it);  // before the callback, which may add requests
            if (on_reply) {
                on_reply(frame);
            }
            return true;
        }
    }
    return false;
}

void RequestTracker::expire(Clock::time_point now) {
    std::vector<ReplyCallback> expired;
    std::erase_if(pending_, [&](Pending& request) {
        if (request.deadline > now) {
            return false;
        }
        expired.push_back(std::move(request.on_reply));
        return true;
    });
    for (auto& on_reply : expired) {
        if (on_reply) {
            on_reply(std::nullopt);
        }
    }
}

void RequestTracker::fail_all() { expire(Clock::time_point::max()); }

}  // namespace djivcam
