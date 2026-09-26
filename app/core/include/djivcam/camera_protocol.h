// Camera settings of the DJI Osmo Action 5 Pro over DUML: command builders, the known value codes
// with their labels, and parsers for replies and status pushes.
//
// Layouts and codes come from docs/camera-controls.md: Mimo's own AC204 code, confirmed value by
// value on the Action 6. Most of them are not yet confirmed on the Action 5 Pro itself, so callers
// confirm every change on the camera's read-back (state), never on the ACK alone.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "djivcam/duml.h"

namespace djivcam::camera {

using duml::Bytes;

// A request for the camera (flags 0x40); most go to the camera itself (0x01).
struct Command {
    std::uint8_t receiver = duml::kAddrCamera;
    std::uint8_t cmd_set = 0;
    std::uint8_t cmd_id = 0;
    Bytes payload;
};

// The settings the app can change. Each has a code list (choices()), a setter (set()) and a
// read-back (CameraState).
enum class Setting {
    Mode,            // shooting mode, 02/E1
    Stabilization,   // parameter 0x0008
    SteadyScene,     // parameter 0x0030: 0 Daily, 1 Sport
    Fov,             // parameter 0x0009
    ExposureMode,    // 02/1E: 1 auto, 4 manual
    Iso,             // 02/2A index
    IsoAutoMax,      // parameter 0x000F
    Ev,              // 02/2E index, 16 = 0 EV, 1/3 EV per step
    Color,           // 02/42
    Texture,         // 02/38, signed
    NoiseReduction,  // 02/44, signed
    AntiFlicker,     // 02/46
    Codec,           // 02/AB: 0 H.264, 1 H.265
};

struct Choice {
    int code;
    std::string_view label;
    // Offered while the camera has not sent its own list; the others only when it lists them.
    bool offered_by_default = true;
};

// Known codes of `setting` with their labels (Mimo's names). The camera's capability lists narrow
// these per mode and format; unknown codes the camera reports are labelled by describe().
std::span<const Choice> choices(Setting setting);
std::string describe(Setting setting, int code);
// "Stabilization", "FOV", ...
std::string_view name(Setting setting);

// Video resolution codes (02/18); the aspect ratio is part of the resolution.
std::span<const Choice> resolutions();
std::span<const Choice> frame_rates();
// Frames per second of a frame rate code (0x03 -> 30); 0 if unknown.
int frames_per_second(int frame_rate);
std::string describe_format(int resolution, int frame_rate);

// --- Commands ---------------------------------------------------------------------------------

Command set(Setting setting, int code);
// 02/18: recording format. `speed_ratio` > 0 for slow motion (e.g. 4 = 4x).
Command set_format(int resolution, int frame_rate, int speed_ratio = 0);
// 02/2C: white balance, `kelvin` 2000-10000 in 100 K steps, 0 = auto.
Command set_white_balance(int kelvin);
// 02/28: shutter 1/`denominator` s (manual exposure only).
Command set_shutter(int denominator);
Command start_recording();
Command stop_recording();
Command take_photo();
// 02/8E GET of a parameter (settings without a status topic: stabilization, FOV, ...).
Command get_parameter(std::uint16_t pid);
// 00/99 to 0x28: subscribe to a status topic; the camera pushes its value now and on each change.
Command subscribe(std::string_view topic, std::uint32_t subscription_id);

// Parameter id of a setting set through 02/8E, if it is one.
std::optional<std::uint16_t> parameter_of(Setting setting);

// Status topics the app subscribes to.
std::span<const std::string_view> status_topics();

// First byte of a reply: 00 success, else an error (describe_result).
std::string describe_result(std::uint8_t result);

// --- State --------------------------------------------------------------------------------------

struct StorageInfo {
    bool present = false;
    int state = 0;  // 0 normal, 1 not inserted, 4 needs formatting, 8 full, ...
    std::uint32_t total_mb = 0;
    std::uint32_t free_mb = 0;
    std::uint32_t video_seconds_left = 0;
};

// What the camera has reported so far (fields stay empty until it does).
struct CameraState {
    std::map<Setting, int> values;
    std::optional<int> resolution;
    std::optional<int> frame_rate;
    std::optional<int> white_balance_kelvin;  // 0 = auto
    std::optional<int> iso_actual;
    std::optional<int> shutter_actual;        // denominator of 1/x s; negative = whole seconds
    bool recording = false;
    bool taking_photo = false;
    bool playback = false;
    std::optional<std::uint32_t> record_seconds;
    std::optional<int> battery_percent;
    bool charging = false;
    std::optional<StorageInfo> storage;
    // Capability lists for the current mode and format, by setting (codes the camera accepts now).
    std::map<Setting, std::vector<int>> allowed;
    // Allowed recording formats: (resolution, frame rate) pairs.
    std::vector<std::pair<int, int>> allowed_formats;
};

// A decoded 00/99 status push.
struct TopicValue {
    std::string topic;
    Bytes value;
};
std::optional<TopicValue> parse_topic_push(const duml::Frame& frame);

// A decoded 02/8E GET reply.
struct ParameterValue {
    std::uint16_t pid = 0;
    Bytes value;
};
std::optional<ParameterValue> parse_parameter_reply(std::span<const std::uint8_t> payload);

// Folds a status message into `state`; true if it changed anything the app shows.
bool apply_topic(CameraState& state, const TopicValue& topic);
bool apply_parameter(CameraState& state, const ParameterValue& parameter);
// Other pushes the camera sends on its own (battery 0D/02).
bool apply_push(CameraState& state, const duml::Frame& frame);

}  // namespace djivcam::camera
