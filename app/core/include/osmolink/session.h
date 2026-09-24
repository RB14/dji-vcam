// Live-view session: keeps a datalink to the camera open and streams its video, reconnecting
// whenever the camera or the network link goes away.
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
#include <optional>
#include <span>
#include <string>
#include <thread>

namespace osmolink {

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
    std::chrono::milliseconds gap_timeout{50};
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

    LiveViewSession(SessionConfig config, VideoCallback on_video, StateCallback on_state);
    ~LiveViewSession();
    LiveViewSession(const LiveViewSession&) = delete;
    LiveViewSession& operator=(const LiveViewSession&) = delete;

    void start();
    void stop();
    SessionState state() const { return state_; }
    SessionStats stats() const;

private:
    void run(std::stop_token stop);
    void set_state(SessionState state, const std::string& detail);

    SessionConfig config_;
    VideoCallback on_video_;
    StateCallback on_state_;
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

}  // namespace osmolink
