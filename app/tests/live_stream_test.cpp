#include "djivcam/live_stream.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

using namespace djivcam;
using live::Resolution;

namespace {

duml::Bytes bytes(std::string_view text) { return {text.begin(), text.end()}; }

duml::Bytes concat(std::initializer_list<duml::Bytes> parts) {
    duml::Bytes out;
    for (const auto& part : parts) {
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

}  // namespace

TEST(LiveStream, JoinPacksLengthPrefixedStrings) {
    // The appendix vector of protocol-notes.md (SSID "MyRouter", password "secret123").
    const auto command = live::join_network("MyRouter", "secret123");
    EXPECT_EQ(command.receiver, duml::kAddrWifi);
    EXPECT_EQ(command.cmd_set, 0x07);
    EXPECT_EQ(command.cmd_id, 0x47);
    EXPECT_EQ(command.payload, concat({{0x08}, bytes("MyRouter"), {0x09}, bytes("secret123")}));
}

TEST(LiveStream, JoinRejectsBadLengths) {
    EXPECT_THROW(live::join_network("", "secret123"), std::invalid_argument);
    EXPECT_THROW(live::join_network(std::string(33, 'n'), "secret123"), std::invalid_argument);
    EXPECT_THROW(live::join_network("net", std::string(64, 'p')), std::invalid_argument);
    EXPECT_NO_THROW(live::join_network("open-network", ""));
}

TEST(LiveStream, StartFollowsMimosLayout) {
    const auto command = live::stream_settings({Resolution::P1080, 6000, "rtmp://192.168.1.20:1935/live/cam"});
    EXPECT_EQ(command.receiver, live::kAddrLive);
    EXPECT_EQ(command.cmd_set, 0x08);
    EXPECT_EQ(command.cmd_id, 0x78);
    const duml::Bytes head = {0x01, 0x8A, 0x00, 0x0A, 0x70, 0x17, 0xFE, 0x01, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x00};
    const std::string json = R"({"rtmpAddress":"rtmp:\/\/192.168.1.20:1935\/live\/cam","watermark":0,"codec":"",)"
                             R"("EnhancedRTMP":false,"supportStopLive":false})";
    EXPECT_EQ(command.payload, concat({head, bytes(json)}));
    EXPECT_EQ(live::stream_settings({Resolution::P720, 4000, "rtmp://h/x"}).payload[3], 0x04);
    EXPECT_EQ(live::stream_settings({Resolution::P720, 4000, "rtmp://h/x"}).payload[4], 0xA0);  // 4000 = 0x0FA0
}

TEST(LiveStream, StartCanAnnounceStopSupport) {
    const auto payload = live::stream_settings({Resolution::P1080, 6000, "rtmp://h/x", true}).payload;
    const std::string text(payload.begin(), payload.end());
    EXPECT_TRUE(text.ends_with(R"("supportStopLive":true})"));
}

TEST(LiveStream, TheSettingsJsonFitsTheCamerasBuffer) {
    // 35 characters with 4 slashes: 127 bytes of JSON, the most the camera takes (DJI Mimo's size).
    EXPECT_TRUE(live::fits({Resolution::P1080, 6000, "rtmp://172.16.100.20:1935/live/vcam"}));
    EXPECT_FALSE(live::fits({Resolution::P1080, 6000, "rtmp://172.16.100.20:1935/live/vcam1"}));
    EXPECT_FALSE(live::fits({Resolution::P1080, 6000, "rtmp://172.16.100.20:1935/live/djivcam"}));  // refused: d6
    EXPECT_TRUE(live::fits({Resolution::P1080, 6000, "rtmp://172.16.100.20/live/vcam"}));
    EXPECT_FALSE(live::fits({Resolution::P1080, 6000, ""}));
    EXPECT_THROW(live::stream_settings({Resolution::P1080, 6000, "rtmp://172.16.100.20:1935/live/vcam1"}),
                 std::invalid_argument);
}

TEST(LiveStream, StartRejectsMissingOrHugeAddresses) {
    EXPECT_THROW(live::stream_settings({Resolution::P1080, 6000, ""}), std::invalid_argument);
    EXPECT_THROW(live::stream_settings({Resolution::P1080, 6000, "rtmp://h/" + std::string(300, 'x')}), std::invalid_argument);
}

TEST(LiveStream, ControlCommandsGoWhereMimoSendsThem) {
    EXPECT_EQ(live::live_mode().receiver, live::kAddrLive);
    EXPECT_EQ(live::live_mode().payload, duml::Bytes{0x1A});
    EXPECT_EQ(live::video_mode().payload, duml::Bytes{0x01});
    EXPECT_EQ(live::stop_stream().payload, (duml::Bytes{0x01, 0x01, 0x1A, 0x00, 0x01, 0x02}));
    EXPECT_EQ(live::start_stream().payload, (duml::Bytes{0x01, 0x01, 0x1A, 0x00, 0x01, 0x01}));
    EXPECT_EQ(live::start_stream().receiver, live::kAddrLive);
    EXPECT_EQ(live::scan_networks().receiver, live::kAddrWifiScan);
    EXPECT_EQ(live::keep_alive().receiver, duml::kAddrSession);
    EXPECT_EQ(live::keep_alive().payload, (duml::Bytes{0x04, 0x00}));
}

TEST(LiveStream, ParsesTheStoredSettings) {
    duml::Bytes reply = {0x00, 0x01, 0x8A, 0x00, 0x04, 0xA0, 0x0F, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x00};
    const auto url = bytes("rtmp://192.168.1.20:1935/live/cam");
    reply.insert(reply.end(), url.begin(), url.end());
    reply.resize(reply.size() + 20, 0x00);  // NUL padding
    const auto settings = live::parse_stream_settings(reply);
    ASSERT_TRUE(settings);
    EXPECT_EQ(*settings, (live::StreamSettings{Resolution::P720, 4000, "rtmp://192.168.1.20:1935/live/cam"}));
}

TEST(LiveStream, RejectsFailedOrUnknownSettings) {
    const duml::Bytes ok = {0x00, 0x01, 0x8A, 0x00, 0x0A, 0x70, 0x17, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x00};
    EXPECT_TRUE(live::parse_stream_settings(ok));
    auto failed = ok;
    failed[0] = 0x01;
    EXPECT_FALSE(live::parse_stream_settings(failed));
    auto unknown = ok;
    unknown[4] = 0x47;  // 480p in Moblin's table: not offered on the Action 5 Pro
    EXPECT_FALSE(live::parse_stream_settings(unknown));
    EXPECT_FALSE(live::parse_stream_settings(duml::Bytes{0x00, 0x01}));
}

TEST(LiveStream, ParsesTheNetworkList) {
    const duml::Bytes list = concat({
        {0x01, 0x11, 0x00, 0x00},
        {0x0E, 0x01, 0x01, 0x01, 0x00, 0x00}, bytes("HomeWiFi"),
        {0x0F, 0x01, 0x01, 0x02, 0x01, 0x00}, bytes("Office-5G"),
        {0x06, 0x01, 0x01, 0x01, 0x00, 0x00},  // no name: skipped
        {0x0B, 0x01, 0x01, 0x01, 0x00, 0x00}, bytes("cut"),  // longer than what is left: the end
    });
    const auto networks = live::parse_network_list(list);
    ASSERT_EQ(networks.size(), 2u);
    EXPECT_EQ(networks[0], (live::Network{"HomeWiFi", false, 0}));
    EXPECT_EQ(networks[1], (live::Network{"Office-5G", true, 1}));
    EXPECT_TRUE(live::parse_network_list(duml::Bytes{0x01, 0x11}).empty());
}

TEST(LiveStream, ParsesTheMac) {
    EXPECT_EQ(live::parse_mac(duml::Bytes{0x00, 0x58, 0xB8, 0x58, 0x00, 0x00, 0x01}), "58:b8:58:00:00:01");
    EXPECT_FALSE(live::parse_mac(duml::Bytes{0x01, 0x58, 0xB8, 0x58, 0x00, 0x00, 0x01}));
    EXPECT_FALSE(live::parse_mac(duml::Bytes{0x00, 0x58}));
}

TEST(LiveStream, AcceptedMeansAZeroFirstByte) {
    duml::Frame reply;
    reply.payload = {0x00, 0x00};
    EXPECT_TRUE(live::accepted(reply));
    reply.payload = {0x01, 0xFF};
    EXPECT_FALSE(live::accepted(reply));
    reply.payload = {};
    EXPECT_FALSE(live::accepted(reply));
}
