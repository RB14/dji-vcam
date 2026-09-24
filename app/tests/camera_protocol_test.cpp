#include "djivcam/camera_protocol.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

namespace djivcam::camera {
namespace {

// The frame docs/camera-controls.md section 5 lists for `command` (app -> receiver, seq 0x1234).
std::string encode(const Command& command) {
    const auto bytes = duml::Frame{duml::kAddrApp, command.receiver, 0x1234, duml::kFlagRequest, command.cmd_set,
                                   command.cmd_id, command.payload}
                           .encode();
    std::string hex;
    for (std::uint8_t byte : bytes) {
        char two[3];
        std::snprintf(two, sizeof(two), "%02x", byte);
        hex += two;
    }
    return hex;
}

duml::Frame push(std::string_view topic, const Bytes& value) {
    Bytes payload = {0x02, 0x06, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const auto total = static_cast<std::uint16_t>(topic.size() + 16 + value.size());
    payload.push_back(static_cast<std::uint8_t>(total));
    payload.push_back(static_cast<std::uint8_t>(total >> 8));
    payload.push_back(static_cast<std::uint8_t>(topic.size()));
    payload.push_back(0x00);
    payload.insert(payload.end(), topic.begin(), topic.end());
    payload.insert(payload.end(), 6, 0x00);
    payload.push_back(static_cast<std::uint8_t>(value.size()));
    payload.push_back(0x00);
    payload.insert(payload.end(), value.begin(), value.end());
    return duml::Frame{0x28, duml::kAddrApp, 1, duml::kFlagPush, 0x00, 0x99, payload};
}

TEST(CameraProtocol, BuildsTheDocumentedFrames) {
    EXPECT_EQ(encode(get_parameter(0x0008)), "551104920201341240028e00010800d47e");
    EXPECT_EQ(encode(get_parameter(0x0009)), "551104920201341240028e000109000c67");
    EXPECT_EQ(encode(set(Setting::Stabilization, 0x03)), "551304030201341240028e01010800010318b7");
    EXPECT_EQ(encode(set(Setting::Stabilization, 0x04)), "551304030201341240028e010108000104a7c3");
    EXPECT_EQ(encode(set(Setting::Fov, 0x02)), "551304030201341240028e0101090001022aba");
    EXPECT_EQ(encode(set(Setting::SteadyScene, 1)), "551304030201341240028e010130000101203d");
    EXPECT_EQ(encode(set(Setting::IsoAutoMax, 5)), "551304030201341240028e01010f0001050f85");
    EXPECT_EQ(encode(set(Setting::Mode, 0x01)), "550e0466020134124002e1014df2");
    EXPECT_EQ(encode(set(Setting::Mode, 0x05)), "550e0466020134124002e10569b4");
    EXPECT_EQ(encode(set(Setting::Mode, 0x28)), "550e0466020134124002e1288e4e");
    EXPECT_EQ(encode(start_recording()), "550e04660201341240020201bc31");
    EXPECT_EQ(encode(stop_recording()), "550e046602013412400202003520");
    EXPECT_EQ(encode(take_photo()), "550e04660201341240020101d41b");
    EXPECT_EQ(encode(set_format(0x10, 0x03)), "551204c70201341240021810030000002d76");
    EXPECT_EQ(encode(set_format(0x67, 0x03)), "551204c70201341240021867030000000253");
    EXPECT_EQ(encode(set_format(0x0A, 0x07, 4)), "551204c7020134124002180a07000400499b");
    EXPECT_EQ(encode(set(Setting::ExposureMode, 4)), "550f04a20201341240021e04001ce7");
    EXPECT_EQ(encode(set(Setting::ExposureMode, 1)), "550f04a20201341240021e0100a499");
    EXPECT_EQ(encode(set(Setting::Iso, 0)), "550e04660201341240022a00c6cd");
    EXPECT_EQ(encode(set(Setting::Iso, 5)), "550e04660201341240022a056b9a");
    EXPECT_EQ(encode(set_shutter(200)), "5514046d0201341240022801c88000000040c173");
    EXPECT_EQ(encode(set(Setting::Ev, 16)), "550e04660201341240022e1027ba");
    EXPECT_EQ(encode(set(Setting::Ev, 18)), "550e04660201341240022e123599");
    EXPECT_EQ(encode(set_white_balance(3400)), "551204c70201341240022c062200000061bd");
    EXPECT_EQ(encode(set_white_balance(0)), "551204c70201341240022c0000000000dc30");
    EXPECT_EQ(encode(set(Setting::AntiFlicker, 2)), "550e046602013412400246022122");
    EXPECT_EQ(encode(set(Setting::Color, 0x00)), "550e046602013412400242005366");
    EXPECT_EQ(encode(set(Setting::Color, 0x3D)), "550e0466020134124002423d358c");
    EXPECT_EQ(encode(set(Setting::Texture, -1)), "550e046602013412400238ff9f64");
    EXPECT_EQ(encode(set(Setting::NoiseReduction, -1)), "550e046602013412400244fffb3d");
    EXPECT_EQ(encode(set(Setting::Codec, 1)), "550f04a2020134124002ab01005b2a");
    EXPECT_EQ(encode(subscribe("cam_status", 0x69df)),
              "552a049c0228341240009902020000df69000000000010000a0063616d5f7374617475730000000024fc");
    EXPECT_EQ(encode(subscribe("camcap_eis", 0x69e5)),
              "552a049c0228341240009902020000e569000000000010000a0063616d6361705f65697300000000ce5f");
}

TEST(CameraProtocol, ParsesTopicPushes) {
    const auto topic = parse_topic_push(push("cam_video_param_v2", {0x10, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}));
    ASSERT_TRUE(topic);
    EXPECT_EQ(topic->topic, "cam_video_param_v2");
    CameraState state;
    EXPECT_TRUE(apply_topic(state, *topic));
    EXPECT_EQ(state.resolution, 0x10);
    EXPECT_EQ(state.frame_rate, 0x03);
    EXPECT_EQ(state.values[Setting::Codec], 1);
    EXPECT_FALSE(apply_topic(state, *topic));  // nothing new
}

TEST(CameraProtocol, RejectsTruncatedPushes) {
    auto frame = push("cam_status", {0x18, 0x10, 0x5F, 0x01, 0x01});
    frame.payload.resize(frame.payload.size() - 3);
    EXPECT_FALSE(parse_topic_push(frame));
    EXPECT_FALSE(parse_topic_push(duml::Frame{0x28, 0x02, 1, 0x00, 0x00, 0x99, {0x02, 0x06}}));
}

TEST(CameraProtocol, DecodesCameraStatusFlags) {
    CameraState state;
    // Recording (bits 3-4), Sport (bit 12), camera type 95, work mode 1, shooting mode Video.
    apply_topic(state, *parse_topic_push(push("cam_status", {0x18, 0x10, 0x5F, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00})));
    EXPECT_TRUE(state.recording);
    EXPECT_FALSE(state.taking_photo);
    EXPECT_FALSE(state.playback);
    EXPECT_EQ(state.values[Setting::SteadyScene], 1);
    EXPECT_EQ(state.values[Setting::Mode], 0x01);
}

TEST(CameraProtocol, DecodesExposureAndImageEffect) {
    CameraState state;
    Bytes expo(46, 0x00);
    expo[5] = 5;       // ISO 400
    expo[6] = 18;      // EV +0.7
    expo[7] = 4;       // manual
    expo[16] = 0x90;   // actual ISO 400
    expo[17] = 0x01;
    expo[20] = 0xC8;   // 1/200 s
    expo[21] = 0x80;
    apply_topic(state, *parse_topic_push(push("cam_expo_param", expo)));
    EXPECT_EQ(state.values[Setting::Iso], 5);
    EXPECT_EQ(state.values[Setting::Ev], 18);
    EXPECT_EQ(state.values[Setting::ExposureMode], 4);
    EXPECT_EQ(state.iso_actual, 400);
    EXPECT_EQ(state.shutter_actual, 200);

    Bytes effect(16, 0x00);
    effect[2] = 0x3D;           // D-Log M
    effect[3] = 2;              // 50 Hz
    effect[4] = 6;              // manual white balance
    effect[5] = 56;             // 5600 K
    effect[14] = 0xFF;          // noise reduction -1
    effect[15] = 0x01;          // texture +1
    apply_topic(state, *parse_topic_push(push("cam_image_effect", effect)));
    EXPECT_EQ(state.values[Setting::Color], 0x3D);
    EXPECT_EQ(state.values[Setting::AntiFlicker], 2);
    EXPECT_EQ(state.white_balance_kelvin, 5600);
    EXPECT_EQ(state.values[Setting::NoiseReduction], -1);
    EXPECT_EQ(state.values[Setting::Texture], 1);
}

TEST(CameraProtocol, DecodesCapabilityListsHonouringCount) {
    CameraState state;
    // [ver][len u16][count][entries...] + a trailing byte to ignore.
    apply_topic(state, *parse_topic_push(push("camcap_eis", {0x01, 0x04, 0x00, 0x03, 0x00, 0x01, 0x03, 0x02})));
    EXPECT_EQ(state.allowed[Setting::Stabilization], (std::vector<int>{0, 1, 3}));
    apply_topic(state, *parse_topic_push(push("camcap_sharpness", {0x01, 0x03, 0x00, 0x02, 0xFE, 0x02})));
    EXPECT_EQ(state.allowed[Setting::Texture], (std::vector<int>{-2, 2}));
    apply_topic(state, *parse_topic_push(push("camcap_video_format", {0x01, 0x07, 0x00, 0x02, 0x10, 0x03, 0x00, 0x0A, 0x06, 0x00})));
    EXPECT_EQ(state.allowed_formats, (std::vector<std::pair<int, int>>{{0x10, 0x03}, {0x0A, 0x06}}));
}

TEST(CameraProtocol, DecodesRecordTimeAndStorage) {
    CameraState state;
    apply_topic(state, *parse_topic_push(push("cam_record_time", {0x2C, 0x01, 0x00, 0x00, 0x00, 0x00})));
    EXPECT_EQ(state.record_seconds, 300u);

    Bytes storage = {0x00, 0x00, 0x01, 0x00};  // current store 0 (SD), one entry
    const Bytes entry = {0x00, 0x01, 0x00, 0x77, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x00,
                         0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0x00};  // present, 30464 MB, 15360 MB, 3600 s
    storage.insert(storage.end(), entry.begin(), entry.end());
    apply_topic(state, *parse_topic_push(push("cam_storage", storage)));
    ASSERT_TRUE(state.storage);
    EXPECT_TRUE(state.storage->present);
    EXPECT_EQ(state.storage->total_mb, 30464u);
    EXPECT_EQ(state.storage->free_mb, 15360u);
    EXPECT_EQ(state.storage->video_seconds_left, 3600u);
}

TEST(CameraProtocol, ParsesParameterReplies) {
    const Bytes reply = {0x00, 0x00, 0x01, 0x09, 0x00, 0x01, 0x05};  // GET FOV = Natural Wide (Action 6 capture)
    const auto parameter = parse_parameter_reply(reply);
    ASSERT_TRUE(parameter);
    EXPECT_EQ(parameter->pid, 0x0009);
    CameraState state;
    EXPECT_TRUE(apply_parameter(state, *parameter));
    EXPECT_EQ(state.values[Setting::Fov], 5);
    EXPECT_FALSE(parse_parameter_reply(Bytes{0xE0}));                    // not supported
    EXPECT_FALSE(parse_parameter_reply(Bytes{0x00, 0x00, 0x01, 0x09, 0x00, 0x04, 0x05}));  // truncated
}

TEST(CameraProtocol, ReadsBatteryPush) {
    Bytes battery(40, 0x00);
    battery[20] = 87;
    battery[32] = 1;
    CameraState state;
    EXPECT_TRUE(apply_push(state, duml::Frame{0x05, duml::kAddrApp, 1, duml::kFlagPush, 0x0D, 0x02, battery}));
    EXPECT_EQ(state.battery_percent, 87);
    EXPECT_TRUE(state.charging);
}

TEST(CameraProtocol, LabelsKnownAndUnknownCodes) {
    EXPECT_EQ(describe(Setting::Stabilization, 0x03), "RockSteady+");
    EXPECT_EQ(describe(Setting::Ev, 16), "0");
    EXPECT_EQ(describe(Setting::Fov, 0x42), "code 0x42");
    EXPECT_EQ(describe_format(0x10, 0x03), "4K 16:9, 30 fps");
    EXPECT_EQ(describe_result(0xD9), "not possible right now (recording, or not in this mode)");
}

}  // namespace
}  // namespace djivcam::camera
