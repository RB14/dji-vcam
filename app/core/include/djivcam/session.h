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
    // Local addresses must be on the camera subnet (the bridge or Wi-Fi adapter has joined its AP).
    std::string camera_subnet_prefix = "192.168.2.";
    // Pairing identifier/token used for the TCP 7001 poke (any approved pair works).
    std::string identifier = "284ae5b8d76b3375a04a6417ad71bea3";
    std::string token = "obsd";
    // No packets at all for this long: the session is dead, reconnect.
    std::chrono::milliseconds silence_timeout{5000};
    // Session alive but no video for this long (e.g. the camera still streams to a previous,
    // abandoned session): reconnect with a fresh handshake.
    std::chrono::milliseconds video_timeout{3000};
    // How long to wait for a missing video datagram (re-sent by the camera) before skipping it.
    // While waiting, the video ACK stays at the gap, which holds back the camera's send window.
    // 0 acknowledges the newest datagram right away, as the Python tools do (measured ~135 ms
    // glass to glass); no live run has seen a gap filled within 50 ms yet.
    std::chrono::milliseconds gap_timeout{0};
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
    using ReplyCallback = RequestTracker::ReplyCallback;

    LiveViewSession(SessionConfig config, VideoCallback on_video, StateCallback on_state);
    ~LiveViewSession();
    LiveViewSession(const LiveViewSession&) = delete;
    LiveViewSession& operator=(const LiveViewSession&) = delete;

    // Set before start().
    void set_message_callback(MessageCallback callback) { on_message_ = std::move(callback); }

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
    VideoCallback on_video_;
    StateCallback on_state_;
    MessageCallback on_message_;
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
