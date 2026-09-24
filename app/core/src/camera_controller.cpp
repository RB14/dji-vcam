#include "djivcam/camera_controller.h"

#include <algorithm>

namespace djivcam::camera {
namespace {

using namespace std::chrono_literals;

constexpr auto kReplyTimeout = 1500ms;

// Settings the camera does not publish in a status topic: read with 02/8E GET.
constexpr Setting kParameterSettings[] = {Setting::Stabilization, Setting::Fov, Setting::IsoAutoMax};

}  // namespace

CameraController::CameraController(Send send, IsStreaming streaming, ChangeCallback on_change, ErrorCallback on_error)
    : send_(std::move(send)),
      streaming_(std::move(streaming)),
      on_change_(std::move(on_change)),
      on_error_(std::move(on_error)) {}

CameraController::CameraController(LiveViewSession& session, ChangeCallback on_change, ErrorCallback on_error)
    : CameraController(
          [&session](const Command& command, RequestTracker::ReplyCallback on_reply) {
              session.request(command.receiver, command.cmd_set, command.cmd_id, command.payload, std::move(on_reply),
                              kReplyTimeout);
          },
          [&session] { return session.state() == SessionState::Streaming; }, std::move(on_change), std::move(on_error)) {}

CameraController::Pending CameraController::change(std::string key, std::string description, Command command,
                                                    std::optional<std::uint16_t> confirm_pid) {
    Pending pending;
    pending.key = std::move(key);
    pending.description = std::move(description);
    pending.command = std::move(command);
    pending.confirm_pid = confirm_pid;
    return pending;
}

CameraController::Pending CameraController::read(std::uint16_t pid, std::string description) {
    Pending pending;
    pending.key = "get:" + std::to_string(pid);  // one read per parameter is enough
    pending.description = std::move(description);
    pending.command = get_parameter(pid);
    pending.is_get = true;
    pending.report_errors = false;
    return pending;
}

CameraController::Pending CameraController::subscription(std::string_view topic, std::uint32_t id) {
    Pending pending;
    pending.key = "subscribe:" + std::string(topic);
    pending.description = "subscribing to " + std::string(topic);
    pending.command = subscribe(topic, id);
    pending.report_errors = false;
    return pending;
}

CameraState CameraController::state() const {
    std::lock_guard lock(mutex_);
    return state_;
}

void CameraController::on_streaming() {
    {
        std::lock_guard lock(mutex_);
        state_ = {};  // the camera may have changed while disconnected: start over
        queue_.clear();
        in_flight_ = false;
        for (const std::string_view topic : status_topics()) {
            queue_.push_back(subscription(topic, next_subscription_++));
        }
    }
    queue_parameter_reads();
    if (on_change_) {
        on_change_();
    }
    send_next();
}

void CameraController::on_message(const duml::Frame& frame) {
    bool changed = false;
    {
        std::lock_guard lock(mutex_);
        if (const auto topic = parse_topic_push(frame)) {
            changed = apply_topic(state_, *topic);
        } else {
            changed = apply_push(state_, frame);
        }
    }
    if (changed && on_change_) {
        on_change_();
    }
}

void CameraController::queue_parameter_reads() {
    for (Setting setting : kParameterSettings) {
        const std::uint16_t pid = *parameter_of(setting);
        enqueue(read(pid, "reading " + std::string(name(setting))));
    }
}

void CameraController::refresh() {
    if (streaming_()) {
        queue_parameter_reads();
        send_next();
    }
}

void CameraController::set(Setting setting, int code) {
    enqueue(change("set:" + std::to_string(static_cast<int>(setting)),
                   std::string(name(setting)) + " " + describe(setting, code), camera::set(setting, code),
                   parameter_of(setting)));
    if (setting == Setting::Mode) {
        queue_parameter_reads();  // a mode change can change stabilization and FOV
    }
    send_next();
}

void CameraController::set_format(int resolution, int frame_rate) {
    enqueue(change("format", describe_format(resolution, frame_rate), camera::set_format(resolution, frame_rate)));
    queue_parameter_reads();  // a format change narrows or resets stabilization and FOV
    send_next();
}

void CameraController::set_white_balance(int kelvin) {
    enqueue(change("white-balance", kelvin > 0 ? "white balance " + std::to_string(kelvin) + " K" : "auto white balance",
                   camera::set_white_balance(kelvin)));
    send_next();
}

void CameraController::set_shutter(int denominator) {
    enqueue(change("shutter", "shutter 1/" + std::to_string(denominator), camera::set_shutter(denominator)));
    send_next();
}

void CameraController::record(bool start) {
    enqueue(change("record", start ? "start recording" : "stop recording", start ? start_recording() : stop_recording()));
    send_next();
}

void CameraController::take_photo() {
    enqueue(change("photo", "take a photo", camera::take_photo()));
    send_next();
}

void CameraController::enqueue(Pending pending) {
    std::lock_guard lock(mutex_);
    const auto same = std::find_if(queue_.begin(), queue_.end(), [&](const Pending& queued) { return queued.key == pending.key; });
    if (same != queue_.end()) {
        *same = std::move(pending);  // only the newest value matters (sliders, repeated clicks)
    } else {
        queue_.push_back(std::move(pending));
    }
}

void CameraController::send_next() {
    Pending next;
    {
        std::lock_guard lock(mutex_);
        if (in_flight_ || queue_.empty()) {
            return;
        }
        if (!streaming_()) {
            queue_.clear();  // re-subscribed on the next on_streaming()
            return;
        }
        next = std::move(queue_.front());
        queue_.pop_front();
        in_flight_ = true;
    }
    const Command command = next.command;
    send_(command, [this, next](std::optional<duml::Frame> reply) { on_reply(next, reply); });
}

void CameraController::on_reply(const Pending& pending, const std::optional<duml::Frame>& reply) {
    std::string error;
    bool changed = false;
    {
        std::lock_guard lock(mutex_);
        in_flight_ = false;
        if (!reply) {
            error = "the camera did not answer (" + pending.description + ")";
        } else if (reply->payload.empty() || reply->payload[0] != 0x00) {
            error = pending.description + ": " + (reply->payload.empty() ? "empty reply" : describe_result(reply->payload[0]));
        } else if (pending.is_get) {
            if (const auto parameter = parse_parameter_reply(reply->payload)) {
                changed = apply_parameter(state_, *parameter);
            }
        }
    }
    if (error.empty() && pending.confirm_pid) {
        enqueue(read(*pending.confirm_pid, "reading back " + pending.description));
    }
    if (!error.empty() && pending.report_errors && streaming_() && on_error_) {
        on_error_(error);
    }
    if (changed && on_change_) {
        on_change_();
    }
    send_next();
}

}  // namespace djivcam::camera
