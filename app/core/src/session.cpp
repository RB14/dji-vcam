#include "osmolink/session.h"

#include <algorithm>
#include <array>
#include <deque>
#include <optional>

#include "osmolink/datalink.h"
#include "osmolink/duml.h"
#include "osmolink/net.h"

namespace osmolink {
namespace {

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
using duml::Bytes;

constexpr std::size_t kVideoSubheaderLen = 12;
constexpr std::size_t kDuplicateWindow = 512;
constexpr std::uint16_t kSeqStep = 8;         // the camera advances its datagram seq by 8
constexpr std::uint16_t kMaxCountedGap = 512;  // larger jumps are stream restarts, not losses
constexpr auto kAckInterval = 30ms;
constexpr auto kHeartbeatInterval = 200ms;
constexpr auto kRegisterInterval = 1000ms;
constexpr auto kTriggerInterval = 2000ms;
constexpr auto kRouteRetry = 500ms;
constexpr auto kConnectRetry = 1000ms;

// 62-byte "APP" device-info blob the camera expects from the app (0x00/0x81).
Bytes app_device_info() {
    Bytes blob(62, 0x00);
    blob[1] = 'A';
    blob[2] = 'P';
    blob[3] = 'P';
    blob[41] = 0x02;
    blob[50] = 0x02;
    blob[51] = 0x08;
    return blob;
}

// 0x00/0x88 "APP presence" registration payload.
const Bytes kRegisterPayload = {0x17, 0x00, 0x46, 0x23, 0x7c, 0x41, 0x50, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02};

void send_device_info(datalink::Link& link) {
    link.send_duml(duml::kAddrDm368Second, 0x00, 0x81, app_device_info(), duml::kFlagWrite);
}

void send_register(datalink::Link& link) { link.send_duml(duml::kAddrDm368First, 0x00, 0x88, kRegisterPayload); }

// The live-view trigger (verified on the Action 5 Pro): device info + 0x00/0x82.
void send_trigger(datalink::Link& link) {
    send_device_info(link);
    link.send_duml(duml::kAddrDm368Second, 0x00, 0x82, Bytes{0x00}, duml::kFlagWrite);
}

void send_heartbeat(datalink::Link& link, std::uint8_t counter) {
    link.send_duml(duml::kAddrDm368Second, 0x00, 0x4F, Bytes{0x01, 0x00, counter, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF});
}

// Every request from the camera must be answered or it drops the session.
void answer_requests(datalink::Link& link, const datalink::Datagram& datagram) {
    for (const duml::Frame& frame : link.duml_frames(datagram)) {
        if (!frame.is_request()) {
            continue;
        }
        const bool device_info = frame.cmd_set == 0x00 && frame.cmd_id == 0x81;
        link.send_frame(frame.reply(device_info ? app_device_info() : frame.payload));
    }
}

}  // namespace

const char* to_string(SessionState state) {
    switch (state) {
    case SessionState::Stopped: return "stopped";
    case SessionState::WaitingForRoute: return "waiting for camera network";
    case SessionState::Connecting: return "connecting";
    case SessionState::Streaming: return "streaming";
    }
    return "?";
}

LiveViewSession::LiveViewSession(SessionConfig config, VideoCallback on_video, StateCallback on_state)
    : config_(std::move(config)), on_video_(std::move(on_video)), on_state_(std::move(on_state)) {}

LiveViewSession::~LiveViewSession() { stop(); }

void LiveViewSession::start() {
    if (!thread_.joinable()) {
        thread_ = std::jthread([this](std::stop_token stop) { run(stop); });
    }
}

void LiveViewSession::stop() {
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
    set_state(SessionState::Stopped, "");
}

SessionStats LiveViewSession::stats() const {
    return SessionStats{datagrams_, video_datagrams_, video_bytes_, duplicates_, lost_, reordered_, reconnects_};
}

void LiveViewSession::set_state(SessionState state, const std::string& detail) {
    if (state_.exchange(state) != state || !detail.empty()) {
        if (on_state_) {
            on_state_(state, detail);
        }
    }
}

void LiveViewSession::track_sequence(std::optional<std::uint16_t>& highest, std::uint16_t seq) {
    if (!highest) {
        highest = seq;
        return;
    }
    const auto ahead = static_cast<std::uint16_t>(seq - *highest);
    if (ahead != 0 && ahead < 0x8000) {
        if (ahead <= kMaxCountedGap && ahead % kSeqStep == 0) {
            lost_ += ahead / kSeqStep - 1;
        }
        highest = seq;
    } else if (ahead != 0) {
        ++reordered_;
        if (lost_ > 0) {
            --lost_;  // a late arrival, not a loss after all
        }
    }
}

void LiveViewSession::run(std::stop_token stop) {
    bool first_attempt = true;
    while (!stop.stop_requested()) {
        if (!first_attempt) {
            ++reconnects_;
        }
        first_attempt = false;

        // 1. Wait until this host has an address on the camera subnet.
        set_state(SessionState::WaitingForRoute, "");
        std::optional<std::string> local_ip;
        while (!stop.stop_requested()) {
            local_ip = net::local_ip_towards(config_.camera_ip, config_.port);
            if (local_ip && local_ip->starts_with(config_.camera_subnet_prefix)) {
                break;
            }
            std::this_thread::sleep_for(kRouteRetry);
        }
        if (stop.stop_requested()) {
            break;
        }

        // 2. Open the datalink and start the live view.
        set_state(SessionState::Connecting, "via " + *local_ip);
        auto socket = net::UdpSocket::open(*local_ip);
        if (!socket) {
            set_state(SessionState::Connecting, "cannot open UDP socket on " + *local_ip);
            std::this_thread::sleep_for(kConnectRetry);
            continue;
        }
        datalink::Link link(std::move(*socket), config_.camera_ip, config_.port);
        link.poke(config_.identifier, config_.token);
        if (!link.handshake()) {
            set_state(SessionState::Connecting, "no handshake reply");
            std::this_thread::sleep_for(kConnectRetry);
            continue;
        }
        link.settle();
        send_device_info(link);
        link.receive_all(300ms);
        send_register(link);
        link.receive_all(300ms);
        link.send_ack();
        send_trigger(link);
        set_state(SessionState::Streaming, "");

        // 3. Stream until stopped or the camera goes silent.
        std::deque<std::uint16_t> recent_video;
        std::optional<std::uint16_t> highest_video;
        std::uint8_t heartbeat_counter = 0;
        unsigned heartbeat_ticks = 0;
        auto now = Clock::now();
        auto last_packet = now;
        auto last_video = now;
        auto next_ack = now;
        auto next_heartbeat = now;
        auto next_register = now + kRegisterInterval;
        auto next_trigger = now + kTriggerInterval;
        while (!stop.stop_requested()) {
            if (auto datagram = link.receive(10ms)) {
                last_packet = Clock::now();
                ++datagrams_;
                if (datagram->type == datalink::PacketType::Video) {
                    last_video = last_packet;
                    if (std::find(recent_video.begin(), recent_video.end(), datagram->seq) != recent_video.end()) {
                        ++duplicates_;
                    } else {
                        track_sequence(highest_video, datagram->seq);
                        recent_video.push_back(datagram->seq);
                        if (recent_video.size() > kDuplicateWindow) {
                            recent_video.pop_front();
                        }
                        const auto payload = datagram->payload();
                        if (payload.size() > kVideoSubheaderLen) {
                            const auto video = payload.subspan(kVideoSubheaderLen);
                            ++video_datagrams_;
                            video_bytes_ += video.size();
                            if (on_video_) {
                                on_video_(video);
                            }
                        }
                    }
                } else {
                    answer_requests(link, *datagram);
                }
            }

            now = Clock::now();
            if (now - last_packet > config_.silence_timeout) {
                set_state(SessionState::WaitingForRoute, "camera went silent, reconnecting");
                break;
            }
            if (now - last_video > config_.video_timeout) {
                set_state(SessionState::WaitingForRoute, "no video, reconnecting");
                break;
            }
            if (now >= next_ack) {
                link.send_ack();
                next_ack = now + kAckInterval;
            }
            if (now >= next_heartbeat) {
                send_heartbeat(link, heartbeat_counter);
                if (++heartbeat_ticks % 2 == 0) {
                    ++heartbeat_counter;
                }
                next_heartbeat = now + kHeartbeatInterval;
            }
            if (now >= next_register) {
                send_register(link);
                next_register = now + kRegisterInterval;
            }
            if (now >= next_trigger) {
                send_trigger(link);
                next_trigger = now + kTriggerInterval;
            }
        }
    }
}

}  // namespace osmolink
