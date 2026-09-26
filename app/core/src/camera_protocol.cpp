#include "djivcam/camera_protocol.h"

#include <cstdio>

namespace djivcam::camera {
namespace {

// Parameter ids of 02/8E (docs/camera-controls.md 2.2).
constexpr std::uint16_t kPidStabilization = 0x0008;
constexpr std::uint16_t kPidFov = 0x0009;
constexpr std::uint16_t kPidIsoAutoMax = 0x000F;
constexpr std::uint16_t kPidSteadyScene = 0x0030;

constexpr Choice kModes[] = {
    {0x01, "Video"},      {0x05, "Photo"},      {0x00, "Slow motion"},          {0x02, "Timelapse"},
    {0x0A, "Hyperlapse"}, {0x28, "SuperNight"}, {0x34, "Subject tracking", false}, {0x0C, "Panorama", false},
    {0x1A, "Live streaming", false},            {0x23, "Webcam (USB)", false},
};
constexpr Choice kStabilization[] = {
    {0x00, "Off"},          {0x01, "RockSteady"},       {0x03, "RockSteady+"}, {0x02, "HorizonSteady"},
    {0x04, "HorizonBalancing"}, {0x07, "HorizonCorrection", false},               {0x08, "RockSteady Auto", false},
};
constexpr Choice kSteadyScene[] = {{0, "Daily"}, {1, "Sport"}};
constexpr Choice kFov[] = {
    {0x00, "Ultra Wide"}, {0x01, "Wide"}, {0x02, "Standard (dewarp)"}, {0x05, "Natural Wide", false},
    {0x06, "Portrait Flat", false}, {0x03, "Narrow", false}, {0x04, "Extreme Wide", false},
};
constexpr Choice kExposureModes[] = {{1, "Auto"}, {4, "Manual"}, {2, "Shutter priority", false}, {3, "Aperture priority", false}};
constexpr Choice kIso[] = {
    {0, "Auto"},  {3, "100"},  {4, "200"},   {5, "400"},    {6, "800"},           {7, "1600"},
    {8, "3200"}, {9, "6400"}, {10, "12800"}, {11, "25600"}, {12, "51200", false},
};
constexpr Choice kIsoAutoMax[] = {
    {1, "100", false}, {2, "200", false}, {3, "400", false}, {4, "800"},   {5, "1600"},
    {6, "3200"},       {7, "6400"},       {8, "12800"},      {9, "25600"},
};
constexpr Choice kEv[] = {
    {1, "-5.0", false},  {2, "-4.7", false},  {3, "-4.3", false},  {4, "-4.0", false},  {5, "-3.7", false},
    {6, "-3.3", false},  {7, "-3.0"},         {8, "-2.7"},         {9, "-2.3"},         {10, "-2.0"},
    {11, "-1.7"},        {12, "-1.3"},        {13, "-1.0"},        {14, "-0.7"},        {15, "-0.3"},
    {16, "0"},           {17, "+0.3"},        {18, "+0.7"},        {19, "+1.0"},        {20, "+1.3"},
    {21, "+1.7"},        {22, "+2.0"},        {23, "+2.3"},        {24, "+2.7"},        {25, "+3.0"},
    {26, "+3.3", false}, {27, "+3.7", false}, {28, "+4.0", false}, {29, "+4.3", false}, {30, "+4.7", false},
    {31, "+5.0", false},
};
constexpr Choice kColors[] = {{0x00, "Normal (8-bit)"}, {0x3F, "Normal (10-bit)"}, {0x3C, "HLG (10-bit)"}, {0x3D, "D-Log M (10-bit)"}};
constexpr Choice kSigned[] = {{-2, "-2"}, {-1, "-1"}, {0, "0"}, {1, "+1"}, {2, "+2"}};
constexpr Choice kAntiFlicker[] = {{0, "Auto"}, {1, "60 Hz"}, {2, "50 Hz"}, {3, "Off"}};
constexpr Choice kCodecs[] = {{0, "H.264"}, {1, "H.265"}};

constexpr Choice kResolutions[] = {
    {0x10, "4K 16:9"},        {0x67, "4K 4:3"},   {0x2D, "2.7K 16:9"},        {0x5F, "2.7K 4:3"},
    {0x0A, "1080p 16:9"},     {0x6D, "4K 9:16", false}, {0x43, "2.7K 9:16", false}, {0x42, "1080p 9:16", false},
    {0x04, "720p", false},
};
constexpr Choice kFrameRates[] = {
    {0x01, "24"}, {0x02, "25"},  {0x03, "30"},  {0x04, "48"},  {0x05, "50"},
    {0x06, "60"}, {0x0A, "100"}, {0x07, "120"}, {0x13, "200"}, {0x08, "240"},
};

constexpr std::string_view kTopics[] = {
    "cam_status",          "cam_video_param_v2", "cam_expo_param", "cam_image_effect", "cam_record_time",
    "cam_storage",         "camcap_base",        "camcap_video_format", "camcap_eis",  "camcap_fov",
    "camcap_iso",          "camcap_iso_auto_max", "camcap_color_mode", "camcap_antiflicker", "camcap_sharpness",
    "camcap_denoise",
};

// Capability topics that list the codes of one setting, one byte per entry.
constexpr std::pair<std::string_view, Setting> kCapabilityTopics[] = {
    {"camcap_base", Setting::Mode},
    {"camcap_eis", Setting::Stabilization},
    {"camcap_fov", Setting::Fov},
    {"camcap_iso", Setting::Iso},
    {"camcap_iso_auto_max", Setting::IsoAutoMax},
    {"camcap_color_mode", Setting::Color},
    {"camcap_antiflicker", Setting::AntiFlicker},
    {"camcap_sharpness", Setting::Texture},
    {"camcap_denoise", Setting::NoiseReduction},
};

std::string hex_code(int code) {
    char text[16];
    std::snprintf(text, sizeof(text), "code 0x%02X", code & 0xFF);
    return text;
}

std::string label_of(std::span<const Choice> list, int code) {
    for (const Choice& choice : list) {
        if (choice.code == code) {
            return std::string(choice.label);
        }
    }
    return hex_code(code);
}

void put_le16(Bytes& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}

std::uint16_t le16(std::span<const std::uint8_t> data, std::size_t at) {
    return static_cast<std::uint16_t>(data[at] | (data[at + 1] << 8));
}

std::uint32_t le32(std::span<const std::uint8_t> data, std::size_t at) {
    return static_cast<std::uint32_t>(data[at]) | (static_cast<std::uint32_t>(data[at + 1]) << 8) |
           (static_cast<std::uint32_t>(data[at + 2]) << 16) | (static_cast<std::uint32_t>(data[at + 3]) << 24);
}

Command parameter_set(std::uint16_t pid, std::uint8_t value) {
    Bytes payload = {0x01, 0x01};
    put_le16(payload, pid);
    payload.push_back(0x01);
    payload.push_back(value);
    return {duml::kAddrCamera, 0x02, 0x8E, std::move(payload)};
}

bool store(CameraState& state, Setting setting, int value) {
    const auto [it, added] = state.values.try_emplace(setting, value);
    if (!added && it->second == value) {
        return false;
    }
    it->second = value;
    return true;
}

template <typename T>
bool store(std::optional<T>& field, T value) {
    if (field == value) {
        return false;
    }
    field = value;
    return true;
}

bool store(bool& field, bool value) {
    const bool changed = field != value;
    field = value;
    return changed;
}

// Capability list: [ver u8][len u16][count u8][count x entry of `entry_size` bytes].
std::vector<std::span<const std::uint8_t>> capability_entries(std::span<const std::uint8_t> value, std::size_t entry_size) {
    std::vector<std::span<const std::uint8_t>> entries;
    if (value.size() < 4) {
        return entries;
    }
    const std::size_t count = value[3];
    for (std::size_t i = 0; i < count && 4 + (i + 1) * entry_size <= value.size(); ++i) {
        entries.push_back(value.subspan(4 + i * entry_size, entry_size));
    }
    return entries;
}

bool apply_capability(CameraState& state, Setting setting, std::span<const std::uint8_t> value) {
    const bool is_signed = setting == Setting::Texture || setting == Setting::NoiseReduction;
    std::vector<int> codes;
    for (const auto entry : capability_entries(value, 1)) {
        codes.push_back(is_signed ? static_cast<std::int8_t>(entry[0]) : entry[0]);
    }
    auto& allowed = state.allowed[setting];
    if (allowed == codes) {
        return false;
    }
    allowed = std::move(codes);
    return true;
}

}  // namespace

std::span<const Choice> choices(Setting setting) {
    switch (setting) {
    case Setting::Mode: return kModes;
    case Setting::Stabilization: return kStabilization;
    case Setting::SteadyScene: return kSteadyScene;
    case Setting::Fov: return kFov;
    case Setting::ExposureMode: return kExposureModes;
    case Setting::Iso: return kIso;
    case Setting::IsoAutoMax: return kIsoAutoMax;
    case Setting::Ev: return kEv;
    case Setting::Color: return kColors;
    case Setting::Texture: return kSigned;
    case Setting::NoiseReduction: return kSigned;
    case Setting::AntiFlicker: return kAntiFlicker;
    case Setting::Codec: return kCodecs;
    }
    return {};
}

std::string describe(Setting setting, int code) { return label_of(choices(setting), code); }

std::string_view name(Setting setting) {
    switch (setting) {
    case Setting::Mode: return "Mode";
    case Setting::Stabilization: return "Stabilization";
    case Setting::SteadyScene: return "Scene";
    case Setting::Fov: return "FOV";
    case Setting::ExposureMode: return "Exposure";
    case Setting::Iso: return "ISO";
    case Setting::IsoAutoMax: return "Auto ISO limit";
    case Setting::Ev: return "EV";
    case Setting::Color: return "Color";
    case Setting::Texture: return "Texture";
    case Setting::NoiseReduction: return "Noise reduction";
    case Setting::AntiFlicker: return "Anti-flicker";
    case Setting::Codec: return "Codec";
    }
    return "?";
}

std::span<const Choice> resolutions() { return kResolutions; }
std::span<const Choice> frame_rates() { return kFrameRates; }

int frames_per_second(int frame_rate) {
    for (const Choice& choice : kFrameRates) {
        if (choice.code == frame_rate) {
            int fps = 0;
            for (char digit : choice.label) {
                fps = fps * 10 + (digit - '0');
            }
            return fps;
        }
    }
    return 0;
}

std::string describe_format(int resolution, int frame_rate) {
    return label_of(kResolutions, resolution) + ", " + label_of(kFrameRates, frame_rate) + " fps";
}

std::optional<std::uint16_t> parameter_of(Setting setting) {
    switch (setting) {
    case Setting::Stabilization: return kPidStabilization;
    case Setting::Fov: return kPidFov;
    case Setting::IsoAutoMax: return kPidIsoAutoMax;
    case Setting::SteadyScene: return kPidSteadyScene;
    default: return std::nullopt;
    }
}

Command set(Setting setting, int code) {
    const auto byte = static_cast<std::uint8_t>(code);  // signed settings travel as s8
    if (const auto pid = parameter_of(setting)) {
        return parameter_set(*pid, byte);
    }
    switch (setting) {
    case Setting::Mode: return {duml::kAddrCamera, 0x02, 0xE1, {byte}};
    case Setting::ExposureMode: return {duml::kAddrCamera, 0x02, 0x1E, {byte, 0x00}};
    case Setting::Iso: return {duml::kAddrCamera, 0x02, 0x2A, {byte}};
    case Setting::Ev: return {duml::kAddrCamera, 0x02, 0x2E, {byte}};
    case Setting::Color: return {duml::kAddrCamera, 0x02, 0x42, {byte}};
    case Setting::Texture: return {duml::kAddrCamera, 0x02, 0x38, {byte}};
    case Setting::NoiseReduction: return {duml::kAddrCamera, 0x02, 0x44, {byte}};
    case Setting::AntiFlicker: return {duml::kAddrCamera, 0x02, 0x46, {byte}};
    case Setting::Codec: return {duml::kAddrCamera, 0x02, 0xAB, {byte, 0x00}};
    default: return {};
    }
}

Command set_format(int resolution, int frame_rate, int speed_ratio) {
    Bytes payload = {static_cast<std::uint8_t>(resolution), static_cast<std::uint8_t>(frame_rate), 0x00};
    put_le16(payload, static_cast<std::uint16_t>(speed_ratio));
    return {duml::kAddrCamera, 0x02, 0x18, std::move(payload)};
}

Command set_white_balance(int kelvin) {
    Bytes payload = {static_cast<std::uint8_t>(kelvin > 0 ? 6 : 0)};  // 6 manual (Kelvin), 0 auto
    put_le16(payload, static_cast<std::uint16_t>(kelvin > 0 ? kelvin / 100 : 0));
    put_le16(payload, 0);  // tint: Mimo always sends 0
    return {duml::kAddrCamera, 0x02, 0x2C, std::move(payload)};
}

Command set_shutter(int denominator) {
    Bytes payload = {0x01};
    put_le16(payload, static_cast<std::uint16_t>(0x8000 | (denominator & 0x7FFF)));  // bit 15: 1/x
    payload.insert(payload.end(), {0x00, 0x00, 0x00, 0x40});  // decimals, then Mimo's constants
    return {duml::kAddrCamera, 0x02, 0x28, std::move(payload)};
}

Command start_recording() { return {duml::kAddrCamera, 0x02, 0x02, {0x01}}; }
Command stop_recording() { return {duml::kAddrCamera, 0x02, 0x02, {0x00}}; }
Command take_photo() { return {duml::kAddrCamera, 0x02, 0x01, {0x01}}; }

Command get_parameter(std::uint16_t pid) {
    Bytes payload = {0x00, 0x01};
    put_le16(payload, pid);
    return {duml::kAddrCamera, 0x02, 0x8E, std::move(payload)};
}

Command subscribe(std::string_view topic, std::uint32_t subscription_id) {
    Bytes payload = {0x02, 0x02, 0x00, 0x00};
    for (int shift = 0; shift < 32; shift += 8) {
        payload.push_back(static_cast<std::uint8_t>(subscription_id >> shift));
    }
    payload.insert(payload.end(), {0x00, 0x00, 0x00});
    put_le16(payload, static_cast<std::uint16_t>(topic.size() + 6));
    put_le16(payload, static_cast<std::uint16_t>(topic.size()));
    payload.insert(payload.end(), topic.begin(), topic.end());
    payload.insert(payload.end(), {0x00, 0x00, 0x00, 0x00});
    return {duml::kAddrDm368First, 0x00, 0x99, std::move(payload)};
}

std::span<const std::string_view> status_topics() { return kTopics; }

std::string describe_result(std::uint8_t result) {
    switch (result) {
    case 0x00: return "OK";
    case 0xD6: return "the camera is busy or in playback";
    case 0xD8: return "the camera is not ready";
    case 0xD9: return "not possible right now (recording, or not in this mode)";
    case 0xDF: return "value not accepted";
    case 0xE0: return "not supported by this camera";
    case 0xE1: return "the camera is busy, try again";
    case 0xE3: return "missing value";
    default: {
        char text[40];
        std::snprintf(text, sizeof(text), "error 0x%02X", result);
        return text;
    }
    }
}

std::optional<TopicValue> parse_topic_push(const duml::Frame& frame) {
    const auto& p = frame.payload;
    if (frame.cmd_set != 0x00 || frame.cmd_id != 0x99 || p.size() < 15 || p[0] != 0x02 || p[1] != 0x06) {
        return std::nullopt;
    }
    const std::size_t name_len = le16(p, 13);
    const std::size_t value_at = 15 + name_len + 6 + 2;
    if (value_at > p.size()) {
        return std::nullopt;
    }
    const std::size_t value_len = le16(p, value_at - 2);
    if (value_at + value_len > p.size()) {
        return std::nullopt;
    }
    return TopicValue{std::string(p.begin() + 15, p.begin() + 15 + static_cast<std::ptrdiff_t>(name_len)),
                      Bytes(p.begin() + static_cast<std::ptrdiff_t>(value_at),
                            p.begin() + static_cast<std::ptrdiff_t>(value_at + value_len))};
}

std::optional<ParameterValue> parse_parameter_reply(std::span<const std::uint8_t> payload) {
    // [ret] 00 01 [pid u16] [len] [value]
    if (payload.size() < 6 || payload[0] != 0x00) {
        return std::nullopt;
    }
    const std::size_t len = payload[5];
    if (6 + len > payload.size()) {
        return std::nullopt;
    }
    return ParameterValue{le16(payload, 3), Bytes(payload.begin() + 6, payload.begin() + 6 + static_cast<std::ptrdiff_t>(len))};
}

bool apply_topic(CameraState& state, const TopicValue& topic) {
    const std::span<const std::uint8_t> v = topic.value;
    const std::string& name = topic.topic;
    bool changed = false;
    if (name == "cam_status" && v.size() >= 5) {
        const std::uint16_t flags = le16(v, 0);
        changed |= store(state.taking_photo, (flags & 0x0007) != 0);
        changed |= store(state.recording, (flags & 0x0018) != 0);
        changed |= store(state.playback, (flags & 0x0100) != 0);
        changed |= store(state, Setting::SteadyScene, (flags >> 12) & 1);
        changed |= store(state, Setting::Mode, v[4]);
    } else if (name == "cam_video_param_v2" && v.size() >= 2) {
        changed |= store(state.resolution, int{v[0]});
        changed |= store(state.frame_rate, int{v[1]});
        if (v.size() > 8) {
            changed |= store(state, Setting::Codec, v[8] & 0x0F);
        }
    } else if (name == "cam_expo_param" && v.size() >= 8) {
        changed |= store(state, Setting::Iso, v[5]);
        changed |= store(state, Setting::Ev, v[6]);  // the configured EV on the Action 6
        changed |= store(state, Setting::ExposureMode, v[7]);
        if (v.size() >= 18) {
            changed |= store(state.iso_actual, int{le16(v, 16)});
        }
        if (v.size() >= 23) {
            const std::uint16_t shutter = le16(v, 20);
            const int amount = shutter & 0x7FFF;
            changed |= store(state.shutter_actual, (shutter & 0x8000) ? amount : -amount);
        }
    } else if (name == "cam_image_effect" && v.size() >= 6) {
        changed |= store(state, Setting::Color, v[2]);
        changed |= store(state, Setting::AntiFlicker, v[3]);
        changed |= store(state.white_balance_kelvin, v[4] == 0 ? 0 : v[5] * 100);
        if (v.size() >= 16) {
            changed |= store(state, Setting::NoiseReduction, static_cast<std::int8_t>(v[14]));
            changed |= store(state, Setting::Texture, static_cast<std::int8_t>(v[15]));
        }
    } else if (name == "cam_record_time" && v.size() >= 2) {
        std::uint32_t seconds = le16(v, 0);
        if (v.size() >= 6 && (le16(v, 4) & 0x8000)) {
            seconds |= static_cast<std::uint32_t>(le16(v, 4) & 0x7FFF) << 16;
        }
        changed |= store(state.record_seconds, seconds);
    } else if (name == "cam_storage" && v.size() >= 4) {
        const int current = v[0] >> 4;
        const std::size_t count = v[2] & 0x0F;
        std::optional<StorageInfo> chosen;
        for (std::size_t i = 0; i < count && 4 + (i + 1) * 18 <= v.size(); ++i) {
            const std::size_t at = 4 + i * 18;
            StorageInfo info{(v[at + 1] & 1) != 0, (v[at + 1] >> 1) & 0x0F, le32(v, at + 2), le32(v, at + 6), le32(v, at + 14)};
            if (!chosen || v[at] == current) {
                chosen = info;
            }
        }
        if (chosen) {
            const auto& old = state.storage;
            if (!old || old->present != chosen->present || old->state != chosen->state || old->free_mb != chosen->free_mb ||
                old->total_mb != chosen->total_mb || old->video_seconds_left != chosen->video_seconds_left) {
                state.storage = chosen;
                changed = true;
            }
        }
    } else if (name == "camcap_video_format") {
        std::vector<std::pair<int, int>> formats;
        for (const auto entry : capability_entries(v, 3)) {
            formats.emplace_back(entry[0], entry[1]);
        }
        if (formats != state.allowed_formats) {
            state.allowed_formats = std::move(formats);
            changed = true;
        }
    } else {
        for (const auto& [topic_name, setting] : kCapabilityTopics) {
            if (name == topic_name) {
                changed |= apply_capability(state, setting, v);
            }
        }
    }
    return changed;
}

bool apply_parameter(CameraState& state, const ParameterValue& parameter) {
    if (parameter.value.empty()) {
        return false;
    }
    for (Setting setting : {Setting::Stabilization, Setting::Fov, Setting::IsoAutoMax, Setting::SteadyScene}) {
        if (parameter_of(setting) == parameter.pid) {
            return store(state, setting, parameter.value[0]);
        }
    }
    return false;
}

bool apply_push(CameraState& state, const duml::Frame& frame) {
    if (frame.cmd_set == 0x0D && frame.cmd_id == 0x02 && frame.payload.size() > 32) {  // battery, ~1 Hz
        bool changed = store(state.battery_percent, int{frame.payload[20]});
        changed |= store(state.charging, frame.payload[32] != 0);
        return changed;
    }
    return false;
}

}  // namespace djivcam::camera
