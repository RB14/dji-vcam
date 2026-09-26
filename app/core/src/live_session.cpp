#include "djivcam/live_session.h"

#include <algorithm>

namespace djivcam::live {
namespace {

using namespace std::chrono_literals;

constexpr auto kModeTimeout = 6s;    // the camera answers the mode switch after ~2 s
constexpr auto kJoinTimeout = 30s;   // ~1 s after a scan, ~10 s without
constexpr auto kStartTimeout = 10s;
constexpr auto kShortTimeout = 3s;
constexpr auto kScanRepeat = 6s;     // the list comes seconds after a scan request

const char* describe(Session::State state) {
    switch (state) {
    case Session::State::Idle: return "idle";
    case Session::State::LiveMode: return "Live Streaming mode";
    case Session::State::Joined: return "joined";
    case Session::State::Streaming: return "streaming";
    }
    return "?";
}

}  // namespace

Session::Session(Link& link, Log log) : link_(link), log_(std::move(log)) {}

void Session::say(const std::string& message) const {
    if (log_) {
        log_(message);
    }
}

bool Session::enter_live_mode() {
    const auto reply = link_.request(live_mode(), kModeTimeout);
    if (!reply || !accepted(*reply)) {
        say(reply ? "the camera refused Live Streaming mode" : "no answer to Live Streaming mode");
        return false;
    }
    state_ = State::LiveMode;
    last_keep_alive_ = Clock::now();
    return true;
}

std::vector<Network> Session::scan(std::chrono::milliseconds timeout) {
    if (state_ != State::LiveMode) {
        say(std::string("scan needs Live Streaming mode, not ") + describe(state_));
        return {};
    }
    const auto until = Clock::now() + timeout;
    while (Clock::now() < until) {
        link_.request(scan_networks(), kShortTimeout);
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - Clock::now());
        if (const auto list = link_.wait_for_push(0x07, 0xAC, std::min<std::chrono::milliseconds>(kScanRepeat, left))) {
            return parse_network_list(list->payload);
        }
    }
    return {};
}

std::optional<std::string> Session::wifi_mac() {
    const auto reply = link_.request(live::wifi_mac(), kShortTimeout);
    return reply ? parse_mac(reply->payload) : std::nullopt;
}

bool Session::join(std::string_view ssid, std::string_view password) {
    if (state_ != State::LiveMode) {
        say(std::string("joining needs Live Streaming mode, not ") + describe(state_));
        return false;
    }
    const auto reply = link_.request(join_network(ssid, password), kJoinTimeout);
    if (!reply || !accepted(*reply)) {
        say(reply ? "the camera could not join the network (" + reply->describe() + ")" : "no answer to the join");
        return false;
    }
    state_ = State::Joined;
    last_keep_alive_ = Clock::now();
    return true;
}

bool Session::start_stream(const StreamSettings& settings) {
    if (state_ != State::Joined) {
        say(std::string("the RTMP push needs the join, not ") + describe(state_));
        return false;
    }
    const auto reply = link_.request(live::start_stream(settings), kStartTimeout);
    if (!reply || !accepted(*reply)) {
        say(reply ? "the camera refused the RTMP push" : "no answer to the RTMP push");
        return false;
    }
    state_ = State::Streaming;
    return true;
}

std::optional<StreamSettings> Session::stored_settings() {
    const auto reply = link_.request(read_stream_settings(), kShortTimeout);
    return reply ? parse_stream_settings(reply->payload) : std::nullopt;
}

void Session::keep_alive(Clock::time_point now) {
    if (state_ != State::Idle && now - last_keep_alive_ >= kKeepAliveInterval) {
        link_.send(live::keep_alive());
        last_keep_alive_ = now;
    }
}

void Session::leave() {
    if (state_ == State::Streaming) {
        link_.request(stop_stream(), kShortTimeout);  // also ends the join and Live Streaming mode
    }
    if (state_ != State::Idle) {
        link_.request(video_mode(), kModeTimeout);
    }
    state_ = State::Idle;
}

}  // namespace djivcam::live
