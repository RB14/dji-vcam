// Keeps the camera's settings in sync over a live-view session: subscribes to the camera's status
// topics whenever the session (re)starts streaming, folds the pushes into a CameraState, and applies
// changes one request at a time (the camera answers rapid requests with "busy"), reading settings
// back after a change instead of trusting the ACK. See docs/camera-controls.md section 4.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

#include "djivcam/camera_protocol.h"
#include "djivcam/session.h"

namespace djivcam::camera {

class CameraController {
public:
    // Sends a request to the camera; the reply callback gets the reply or nullopt (timeout, link
    // down). LiveViewSession::request() in the app.
    using Send = std::function<void(const Command&, RequestTracker::ReplyCallback)>;
    using IsStreaming = std::function<bool()>;
    // Both run on the session thread: the state changed, or a request failed (with a readable reason).
    using ChangeCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const std::string&)>;

    CameraController(Send send, IsStreaming streaming, ChangeCallback on_change, ErrorCallback on_error);
    // Talks to the camera through `session`.
    CameraController(LiveViewSession& session, ChangeCallback on_change, ErrorCallback on_error);
    CameraController(const CameraController&) = delete;
    CameraController& operator=(const CameraController&) = delete;

    // Wire these to the session: call on_streaming() when it enters the Streaming state and
    // on_message() from its message callback (both on the session thread).
    void on_streaming();
    void on_message(const duml::Frame& frame);

    // A copy of what the camera has reported (thread-safe).
    CameraState state() const;

    // Changes (thread-safe; ignored with an error when not streaming). A newer change of the same
    // setting replaces one still waiting to be sent.
    void set(Setting setting, int code);
    void set_format(int resolution, int frame_rate);
    void set_white_balance(int kelvin);  // 0 = auto
    void set_shutter(int denominator);   // 1/denominator s, manual exposure
    void record(bool start);
    void take_photo();
    // Re-reads the settings the camera does not push (stabilization, FOV, ...); call now and then.
    void refresh();

private:
    struct Pending {
        std::string key;          // requests with the same key replace each other while queued
        std::string description;  // for error messages
        Command command;
        bool is_get = false;
        std::optional<std::uint16_t> confirm_pid;  // read this parameter back after success
        bool report_errors = true;
    };
    // A change the user asked for: errors are reported.
    static Pending change(std::string key, std::string description, Command command,
                          std::optional<std::uint16_t> confirm_pid = std::nullopt);
    // Background traffic: a parameter read or a topic subscription, failures stay quiet.
    static Pending read(std::uint16_t pid, std::string description);
    static Pending subscription(std::string_view topic, std::uint32_t id);

    void enqueue(Pending pending);
    void send_next();
    void on_reply(const Pending& pending, const std::optional<duml::Frame>& reply);
    void queue_parameter_reads();

    Send send_;
    IsStreaming streaming_;
    ChangeCallback on_change_;
    ErrorCallback on_error_;
    mutable std::mutex mutex_;
    CameraState state_;
    std::deque<Pending> queue_;
    bool in_flight_ = false;
    std::uint32_t next_subscription_ = 0x6900;
};

}  // namespace djivcam::camera
