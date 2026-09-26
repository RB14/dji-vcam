#include "djivcam/session.h"

#include <array>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <optional>

#include "djivcam/datalink.h"
#include "djivcam/duml.h"
#include "djivcam/net.h"
#include "djivcam/reassembler.h"

namespace djivcam {
namespace {

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
using duml::Bytes;

constexpr std::size_t kVideoSubheaderLen = 12;
constexpr auto kAckInterval = 30ms;
constexpr auto kHeartbeatInterval = 200ms;
constexpr auto kRegisterInterval = 1000ms;
constexpr auto kTriggerInterval = 2000ms;
constexpr auto kRouteRetry = 500ms;

// Sleeps, but returns as soon as `stop` is requested.
void pause(std::stop_token stop, std::chrono::milliseconds duration) {
    std::mutex mutex;
    std::condition_variable_any woken;
    std::unique_lock lock(mutex);
    woken.wait_for(lock, stop, duration, [] { return false; });
}
constexpr auto kConnectRetry = 1000ms;
constexpr auto kNoVideoReport = 6000ms;  // diagnostics cadence while a connection gets no video

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

// 0x00/0x88 "APP presence" registration (query_device_information) payload. DJI Mimo fills bytes
// 2 and 4 at run time; fixed ones are accepted (osmosis, PocketShow).
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

// Requests from the camera are answered (unless `answer_requests` is off: an experiment, see
// SessionConfig). Replies to the app's requests go to their callbacks; everything else is reported
// through `on_message`.
void handle_messages(datalink::Link& link, const datalink::Datagram& datagram, RequestTracker& tracker,
                     const LiveViewSession::MessageCallback& on_message, bool answer_requests) {
    for (const duml::Frame& frame : link.duml_frames(datagram)) {
        if (frame.is_request()) {
            if (answer_requests) {
                const bool device_info = frame.cmd_set == 0x00 && frame.cmd_id == 0x81;
                link.send_frame(frame.reply(device_info ? app_device_info() : frame.payload));
            }
        } else if (tracker.resolve(frame)) {
            continue;
        }
        if (on_message) {
            on_message(frame);
        }
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
    : config_(std::move(config)), on_video_(std::move(on_video)), on_state_(std::move(on_state)) {
    connect_allowed_ = config_.connect_allowed;
}

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
    fail_outgoing();
    set_state(SessionState::Stopped, "");
}

void LiveViewSession::request(std::uint8_t receiver, std::uint8_t cmd_set, std::uint8_t cmd_id, duml::Bytes payload,
                              ReplyCallback on_reply, std::chrono::milliseconds timeout, std::uint8_t flags) {
    {
        // Checked under the lock: the session leaves a connection (Streaming or Connecting) before it
        // fails the queue (also under this lock), so a request is either failed with the queue or
        // refused here, never stranded. Connecting: e.g. commands while waiting for video.
        std::lock_guard lock(outgoing_mutex_);
        if (state_ == SessionState::Streaming || state_ == SessionState::Connecting) {
            outgoing_.push_back({receiver, cmd_set, cmd_id, std::move(payload), flags, std::move(on_reply), timeout});
            return;
        }
    }
    if (on_reply) {
        on_reply(std::nullopt);
    }
}

void LiveViewSession::fail_outgoing() {
    std::vector<Outgoing> failed;
    {
        std::lock_guard lock(outgoing_mutex_);
        failed.swap(outgoing_);
    }
    for (auto& request : failed) {
        if (request.on_reply) {
            request.on_reply(std::nullopt);
        }
    }
}

SessionStats LiveViewSession::stats() const {
    return SessionStats{datagrams_, video_datagrams_, video_bytes_, duplicates_, lost_, recovered_, reconnects_};
}

void LiveViewSession::set_state(SessionState state, const std::string& detail) {
    if (state_.exchange(state) != state || !detail.empty()) {
        if (on_state_) {
            on_state_(state, detail);
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

        // 1. Wait until connections are allowed and this computer is on the camera's network.
        set_state(SessionState::WaitingForRoute, "");
        std::optional<std::string> local_ip;
        while (!stop.stop_requested()) {
            if (connect_allowed_) {
                local_ip = net::local_ip_towards(config_.camera_ip, config_.port);
                if (local_ip && net::same_network(*local_ip, config_.camera_ip)) {
                    break;
                }
            }
            pause(stop, kRouteRetry);
        }
        if (stop.stop_requested()) {
            break;
        }

        // 2. Open the datalink and start the live view.
        set_state(SessionState::Connecting, "via " + *local_ip);
        auto socket = net::UdpSocket::open(*local_ip);
        if (!socket) {
            set_state(SessionState::Connecting, "cannot open UDP socket on " + *local_ip);
            pause(stop, kConnectRetry);
            continue;
        }
        datalink::Link link(std::move(*socket), config_.camera_ip, config_.port);
        link.poke(config_.identifier, config_.token);
        if (!link.handshake()) {
            set_state(SessionState::Connecting, "no handshake reply");
            pause(stop, kConnectRetry);
            continue;
        }
        link.settle();
        send_device_info(link);
        link.receive_all(300ms);
        send_register(link);
        link.receive_all(300ms);
        link.send_ack();
        send_trigger(link);
        fail_outgoing();  // queued in a race with the previous connection going down
        set_state(SessionState::Connecting, "waiting for video");  // Streaming once video arrives

        // 3. Stream until stopped or the camera goes silent.
        VideoReassembler reassembler(
            [this](std::span<const std::uint8_t> payload) {
                if (payload.size() <= kVideoSubheaderLen) {
                    return;
                }
                const auto video = payload.subspan(kVideoSubheaderLen);
                ++video_datagrams_;
                video_bytes_ += video.size();
                if (on_video_) {
                    on_video_(video);
                }
            },
            config_.gap_timeout,
            [this](std::uint64_t) {
                if (on_gap_) {
                    on_gap_();
                }
            });
        // Diagnostics: what the camera does while no video comes (does its video cursor move?).
        std::array<std::uint64_t, 8> by_type{};
        std::uint64_t status_frames = 0;
        std::optional<std::uint16_t> first_cursor;
        auto report_no_video = [&](const char* when) {
            if (!on_log_) {
                return;
            }
            char line[256];
            std::snprintf(line, sizeof(line),
                          "%s: datagrams by type 1:%llu 3:%llu 5:%llu other:%llu, status frames %llu, "
                          "camera video cursor %04x -> %04x",
                          when, static_cast<unsigned long long>(by_type[1]), static_cast<unsigned long long>(by_type[3]),
                          static_cast<unsigned long long>(by_type[5]),
                          static_cast<unsigned long long>(by_type[0] + by_type[4] + by_type[6] + by_type[7]),
                          static_cast<unsigned long long>(status_frames), first_cursor.value_or(0), link.video_cursor());
            on_log_(line);
        };
        std::uint8_t heartbeat_counter = 0;
        unsigned heartbeat_ticks = 0;
        const std::uint64_t lost_base = lost_;
        const std::uint64_t recovered_base = recovered_;
        const std::uint64_t duplicates_base = duplicates_;
        auto now = Clock::now();
        auto last_packet = now;
        auto last_video = now;
        bool video_flowing = false;  // video arrived, and has not stalled since
        bool had_video = false;      // on this connection
        bool gave_up = false;
        auto next_ack = now;
        auto next_heartbeat = now;
        auto next_register = now + kRegisterInterval;
        auto next_trigger = now + kTriggerInterval;
        auto next_report = now + kNoVideoReport;
        RequestTracker tracker;
        while (!stop.stop_requested()) {
            std::vector<Outgoing> batch;
            {
                std::lock_guard lock(outgoing_mutex_);
                batch.swap(outgoing_);
            }
            for (auto& request : batch) {
                const std::uint16_t seq =
                    link.send_duml(request.receiver, request.cmd_set, request.cmd_id, std::move(request.payload), request.flags);
                tracker.add(duml::Frame{duml::kAddrApp, request.receiver, seq, request.flags, request.cmd_set, request.cmd_id, {}},
                            std::move(request.on_reply), Clock::now() + request.timeout);
            }

            if (auto datagram = link.receive(10ms)) {
                last_packet = Clock::now();
                ++datagrams_;
                ++by_type[static_cast<std::uint8_t>(datagram->type) & 0x07];
                if (datagram->type == datalink::PacketType::Status && datagram->raw.size() == datalink::kStatusFrameLen) {
                    ++status_frames;
                    if (!first_cursor) {
                        first_cursor = link.video_cursor();
                    }
                }
                if (datagram->type == datalink::PacketType::Video) {
                    last_video = last_packet;
                    if (!video_flowing) {
                        video_flowing = true;
                        had_video = true;
                        if (on_log_) {
                            char line[96];
                            std::snprintf(line, sizeof(line), "video: first datagram seq %04x, camera video cursor %04x",
                                          datagram->seq, link.video_cursor());
                            on_log_(line);
                        }
                        set_state(SessionState::Streaming, "");
                    }
                    reassembler.push(datagram->seq, datagram->payload(), last_packet);
                } else {
                    handle_messages(link, *datagram, tracker, on_message_, config_.answer_requests);
                }
            }

            now = Clock::now();
            tracker.expire(now);
            reassembler.poll(now);
            const auto& video_stats = reassembler.stats();
            lost_ = lost_base + video_stats.skipped;
            recovered_ = recovered_base + video_stats.recovered;
            duplicates_ = duplicates_base + video_stats.duplicates;
            if (now - last_packet > config_.silence_timeout) {
                set_state(SessionState::WaitingForRoute, "camera went silent, reconnecting");
                break;
            }
            if (video_flowing && now - last_video > config_.video_timeout) {
                video_flowing = false;  // the camera still talks: keep the connection, keep asking
                by_type = {};
                status_frames = 0;
                first_cursor.reset();
                next_report = now + kNoVideoReport;
                set_state(SessionState::Connecting, "the camera stopped sending video, waiting for it");
            }
            if (!video_flowing && now >= next_report) {
                report_no_video("no video yet");
                next_report = now + kNoVideoReport;
            }
            if (now - last_video > (had_video ? config_.stalled_video_timeout : config_.no_video_timeout)) {
                report_no_video("giving up on this connection");
                set_state(SessionState::Connecting, "the camera sent no video, connecting again");
                gave_up = true;
                break;
            }
            if (now >= next_ack) {
                link.set_video_ack(reassembler.ack_seq());
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
        if (stop.stop_requested()) {
            set_state(SessionState::Stopped, "");  // before failing requests, so nobody queues new ones
        }
        tracker.fail_all();  // the link is going down: nobody will answer
        fail_outgoing();
        if (gave_up) {
            pause(stop, kConnectRetry);
        }
    }
}

}  // namespace djivcam
