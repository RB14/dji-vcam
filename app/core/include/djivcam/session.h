// Live-view session: keeps a datalink to the camera open and streams its video, reconnecting
// whenever the camera or the network link goes away. While streaming it also carries the app's
// own DUML requests to the camera (camera settings) and hands the camera's status pushes over.
//
// State machine (runs on its own thread):
//   WaitingForRoute -> Connecting (TCP poke, UDP handshake, settle, register, trigger)
//   -> Streaming (heartbeat + ACK loop) -> back to WaitingForRoute when the camera goes silent.
//
// Mirrors tools/dji_liveview.py; see docs/protocol-notes.md section 3.9.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "djivcam/duml.h"
#include "djivcam/request_tracker.h"

namespace djivcam {

enum class SessionState { Stopped, WaitingForRoute, Connecting, Streaming };

const char* to_string(SessionState state);

struct SessionConfig {
    std::string camera_ip = "192.168.2.1";
    std::uint16_t port = 9004;
    // Local addresses must be on the camera's subnet (this computer on the camera's network).
    std::string camera_subnet_prefix = "192.168.2.";
    // Pairing identifier/token used for the TCP 7001 poke. It must be the pair the Bluetooth link
    // paired with: with another token the camera sends no video (tested 2026-09-25).
    std::string identifier = "284ae5b8d76b3375a04a6417ad71bea3";
    std::string token = "obsd";
    // No packets at all for this long: the session is dead, reconnect.
    std::chrono::milliseconds silence_timeout{5000};
    // Camera still talking but no video for this long: report a stall (state Connecting) and keep
    // the connection, asking for video again.
    std::chrono::milliseconds video_timeout{3000};
    // A connection that got no video at all within this long after the live-view trigger is given
    // up (a healthy camera sends video within ~1 s; the log gets what the camera did meanwhile);
    // one whose video stalled is given up after `stalled_video_timeout`. Either way the session
    // connects again after a second. (A camera that still has the app's Bluetooth link open when
    // it is switched off sends no video to any connection after a short power-off: the app hangs
    // up Bluetooth after the wake, protocol-notes.md 3.11.)
    std::chrono::milliseconds no_video_timeout{5000};
    std::chrono::milliseconds stalled_video_timeout{12000};
    // How long to wait for a missing video datagram before skipping it. Keep 0 (acknowledge the
    // newest datagram at once): the Action 5 Pro never re-sends lost video, and holding the ACK at
    // a gap throttles its sending (tested 2026-09-25: 72 dropped datagrams, 0 re-sent, video rate
    // down ~15% with 150 ms). Only for experiments (dji-vcam-cli --gap-wait).
    std::chrono::milliseconds gap_timeout{0};
    // Whether to answer the camera's own requests on the datalink (0x00/0x81 device info, others
    // echoed). Default: answer. (Experiment switch: DJI Mimo answers none of them over Bluetooth.)
    bool answer_requests = true;
    // Whether connections may start (set_connect_allowed() changes it). The camera ties its live
    // view to the app's Bluetooth session: a datalink started before a Bluetooth session, or
    // while one is open, loses its video. With Bluetooth, start blocked and allow connections
    // once the Bluetooth session has woken the camera and hung up.
    bool connect_allowed = true;
};

struct SessionStats {
    std::uint64_t datagrams = 0;
    std::uint64_t video_datagrams = 0;
    std::uint64_t video_bytes = 0;
    std::uint64_t duplicates = 0;
    std::uint64_t lost = 0;       // video datagrams skipped after gap_timeout (lost for good)
    std::uint64_t recovered = 0;  // gaps filled by a late or re-sent datagram
    std::uint64_t reconnects = 0;
};

class LiveViewSession {
public:
    // Called on the session thread with H.264 stream bytes (sub-header already stripped).
    using VideoCallback = std::function<void(std::span<const std::uint8_t>)>;
    // Called on the session thread on every state change, with a human-readable detail.
    using StateCallback = std::function<void(SessionState, const std::string&)>;
    // Called on the session thread with every DUML message from the camera that does not answer a
    // request(): status pushes, and the camera's own requests (already answered by the session).
    using MessageCallback = std::function<void(const duml::Frame&)>;
    // Called on the session thread when video datagrams were lost for good, before the video after
    // the gap is handed over: that video is damaged until the next keyframe.
    using GapCallback = std::function<void()>;
    // Diagnostics about the connection (e.g. what the camera did while no video came).
    using LogCallback = std::function<void(const std::string&)>;
    using ReplyCallback = RequestTracker::ReplyCallback;

    LiveViewSession(SessionConfig config, VideoCallback on_video, StateCallback on_state);
    ~LiveViewSession();
    LiveViewSession(const LiveViewSession&) = delete;
    LiveViewSession& operator=(const LiveViewSession&) = delete;

    // Set before start().
    void set_message_callback(MessageCallback callback) { on_message_ = std::move(callback); }
    void set_gap_callback(GapCallback callback) { on_gap_ = std::move(callback); }
    void set_log_callback(LogCallback callback) { on_log_ = std::move(callback); }
    // Lets new connections start, or holds them (thread-safe; a running connection is kept).
    void set_connect_allowed(bool allowed) { connect_allowed_ = allowed; }

    void start();
    void stop();
    SessionState state() const { return state_; }
    SessionStats stats() const;

    // Sends a DUML request to the camera (thread-safe). `on_reply` gets the reply on the session
    // thread, or nullopt after `timeout` or when the link goes down; right away, on the calling
    // thread, if the session is not streaming.
    void request(std::uint8_t receiver, std::uint8_t cmd_set, std::uint8_t cmd_id, duml::Bytes payload,
                 ReplyCallback on_reply, std::chrono::milliseconds timeout = std::chrono::milliseconds(1500),
                 std::uint8_t flags = duml::kFlagRequest);

private:
    struct Outgoing {
        std::uint8_t receiver;
        std::uint8_t cmd_set;
        std::uint8_t cmd_id;
        duml::Bytes payload;
        std::uint8_t flags;
        ReplyCallback on_reply;
        std::chrono::milliseconds timeout;
    };

    void run(std::stop_token stop);
    void set_state(SessionState state, const std::string& detail);
    // Fails the requests queued by request() but not sent yet.
    void fail_outgoing();

    SessionConfig config_;
    std::atomic<bool> connect_allowed_{true};
    VideoCallback on_video_;
    StateCallback on_state_;
    MessageCallback on_message_;
    GapCallback on_gap_;
    LogCallback on_log_;
    std::mutex outgoing_mutex_;
    std::vector<Outgoing> outgoing_;
    std::atomic<SessionState> state_{SessionState::Stopped};
    std::atomic<std::uint64_t> datagrams_{0};
    std::atomic<std::uint64_t> video_datagrams_{0};
    std::atomic<std::uint64_t> video_bytes_{0};
    std::atomic<std::uint64_t> duplicates_{0};
    std::atomic<std::uint64_t> lost_{0};
    std::atomic<std::uint64_t> recovered_{0};
    std::atomic<std::uint64_t> reconnects_{0};
    std::jthread thread_;
};

}  // namespace djivcam
