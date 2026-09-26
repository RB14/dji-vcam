// The Bluetooth side of the camera on a Wi-Fi network (docs/protocol-notes.md 3.13): Live
// Streaming mode, the join, the optional RTMP push, the keep-alive while joined, and the way back.
//
// The order is the camera's: the join only works in Live Streaming mode, the RTMP push only once
// joined, and stopping the push also ends the join. The camera stays on the network only while the
// Bluetooth link lives; a lost link sends it back to its access point, and the caller starts over.
// Runs over a Link, so the sequence is tested without a camera.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "djivcam/live_stream.h"

namespace djivcam::live {

// The Bluetooth link to the camera, as the session needs it (ble::CameraBle in the app).
class Link {
public:
    virtual ~Link() = default;
    // Sends `command` and waits for the camera's reply; nullopt if none within `timeout`.
    virtual std::optional<duml::Frame> request(const Command& command, std::chrono::milliseconds timeout) = 0;
    // Sends `command` without waiting for a reply.
    virtual void send(const Command& command) = 0;
    // Waits for a request or push the camera sends by itself (e.g. its network list, 07/AC).
    virtual std::optional<duml::Frame> wait_for_push(std::uint8_t cmd_set, std::uint8_t cmd_id,
                                                     std::chrono::milliseconds timeout) = 0;
};

class Session {
public:
    enum class State { Idle, LiveMode, Joined, Streaming };

    using Log = std::function<void(const std::string&)>;
    using Clock = std::chrono::steady_clock;

    explicit Session(Link& link, Log log = {});

    State state() const { return state_; }

    // Live Streaming mode, which scan() and join() need; stops a livestream left from before first.
    bool enter_live_mode();
    // The networks the camera hears, asking again every few seconds until it answers or `timeout`
    // (it pushes the list seconds after each request). Needs Live Streaming mode.
    std::vector<Network> scan(std::chrono::milliseconds timeout);
    // The camera's Wi-Fi MAC ("58:b8:58:..."), to find its address on the network; any state.
    std::optional<std::string> wifi_mac();
    // Joins the network; false if the camera refused or did not answer (e.g. a wrong password).
    // Needs Live Streaming mode.
    bool join(std::string_view ssid, std::string_view password);
    // Starts pushing RTMP (the camera connects to settings.url): the settings, a pause, then the
    // start, as DJI Mimo does; the start is answered once the camera is connected. Needs the join.
    bool start_stream(const StreamSettings& settings);
    // The pause between the settings and the start (1 s; 0 in tests).
    void set_start_pause(std::chrono::milliseconds pause) { start_pause_ = pause; }
    // The livestream settings the camera stores (from the last start).
    std::optional<StreamSettings> stored_settings();
    // Sends the keep-alive when it is due (from Live Streaming mode on, as DJI Mimo does); call at
    // least every kKeepAliveInterval.
    void keep_alive(Clock::time_point now = Clock::now());
    // Ends the push and the join and returns to Video mode (the camera goes back to its access
    // point). Safe in any state.
    void leave();

private:
    void say(const std::string& message) const;

    Link& link_;
    Log log_;
    State state_ = State::Idle;
    Clock::time_point last_keep_alive_{};
    std::chrono::milliseconds start_pause_{1000};
};

}  // namespace djivcam::live
