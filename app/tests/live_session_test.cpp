#include "djivcam/live_session.h"

#include <gtest/gtest.h>

#include <deque>
#include <map>
#include <utility>

using namespace djivcam;
using live::Session;
using State = live::Session::State;

namespace {

// A scripted camera: answers requests from a table (by command), queues pushes, records all sent.
class FakeLink : public live::Link {
public:
    std::map<std::pair<int, int>, duml::Bytes> answers;  // (cmd_set, cmd_id) -> reply payload
    std::deque<duml::Frame> pushes;
    std::vector<live::Command> sent;

    std::optional<duml::Frame> request(const live::Command& command, std::chrono::milliseconds) override {
        sent.push_back(command);
        const auto it = answers.find({command.cmd_set, command.cmd_id});
        if (it == answers.end()) {
            return std::nullopt;
        }
        duml::Frame reply;
        reply.cmd_set = command.cmd_set;
        reply.cmd_id = command.cmd_id;
        reply.payload = it->second;
        return reply;
    }
    void send(const live::Command& command) override { sent.push_back(command); }
    std::optional<duml::Frame> wait_for_push(std::uint8_t cmd_set, std::uint8_t cmd_id,
                                             std::chrono::milliseconds) override {
        for (auto it = pushes.begin(); it != pushes.end(); ++it) {
            if (it->cmd_set == cmd_set && it->cmd_id == cmd_id) {
                auto frame = *it;
                pushes.erase(it);
                return frame;
            }
        }
        return std::nullopt;
    }

    // The commands sent, as "set/id".
    std::vector<std::pair<int, int>> sequence() const {
        std::vector<std::pair<int, int>> out;
        for (const auto& command : sent) {
            out.emplace_back(command.cmd_set, command.cmd_id);
        }
        return out;
    }
};

// A camera that accepts everything.
FakeLink willing_camera() {
    FakeLink link;
    link.answers[{0x02, 0xE1}] = {0x00};
    link.answers[{0x07, 0x47}] = {0x00, 0x00};
    link.answers[{0x08, 0x78}] = {0x00};
    link.answers[{0x02, 0x8E}] = {0x00};
    link.answers[{0x07, 0x0C}] = {0x00, 0x58, 0xB8, 0x58, 0x00, 0x00, 0x01};
    return link;
}

const live::StreamSettings kStream{live::Resolution::P1080, 6000, "rtmp://192.168.1.20:1935/live/cam"};

}  // namespace

TEST(LiveSession, JoinsInLiveStreamingMode) {
    auto link = willing_camera();
    Session session(link);
    session.set_start_pause(std::chrono::milliseconds(0));
    ASSERT_TRUE(session.enter_live_mode());
    EXPECT_EQ(session.state(), State::LiveMode);
    ASSERT_TRUE(session.join("HomeWiFi", "secret123"));
    EXPECT_EQ(session.state(), State::Joined);
    EXPECT_EQ(session.wifi_mac(), "58:b8:58:00:00:01");
    EXPECT_EQ(link.sequence(), (std::vector<std::pair<int, int>>{{0x02, 0x8E}, {0x02, 0xE1}, {0x07, 0x47}, {0x07, 0x0C}}));
    EXPECT_EQ(link.sent[0].payload, live::stop_stream().payload);  // a livestream left from before
}

TEST(LiveSession, JoinNeedsLiveStreamingMode) {
    auto link = willing_camera();
    Session session(link);
    session.set_start_pause(std::chrono::milliseconds(0));
    EXPECT_FALSE(session.join("HomeWiFi", "secret123"));  // outside it the camera fails (answer 01 ff)
    EXPECT_TRUE(link.sent.empty());
    EXPECT_EQ(session.state(), State::Idle);
}

TEST(LiveSession, RefusedOrSilentJoinKeepsLiveStreamingMode) {
    auto link = willing_camera();
    link.answers[{0x07, 0x47}] = {0x01, 0xFF};
    Session session(link);
    session.set_start_pause(std::chrono::milliseconds(0));
    ASSERT_TRUE(session.enter_live_mode());
    EXPECT_FALSE(session.join("HomeWiFi", "wrong"));
    EXPECT_EQ(session.state(), State::LiveMode);
    link.answers.erase({0x07, 0x47});
    EXPECT_FALSE(session.join("HomeWiFi", "secret123"));
    EXPECT_EQ(session.state(), State::LiveMode);
}

TEST(LiveSession, StreamsOnlyOnceJoined) {
    auto link = willing_camera();
    Session session(link);
    session.set_start_pause(std::chrono::milliseconds(0));
    EXPECT_FALSE(session.start_stream(kStream));
    ASSERT_TRUE(session.enter_live_mode());
    EXPECT_FALSE(session.start_stream(kStream));
    ASSERT_TRUE(session.join("HomeWiFi", "secret123"));
    link.sent.clear();
    ASSERT_TRUE(session.start_stream(kStream));
    EXPECT_EQ(session.state(), State::Streaming);
    ASSERT_EQ(link.sent.size(), 2u);  // the settings, then the start
    EXPECT_EQ(link.sent[0].payload, live::stream_settings(kStream).payload);
    EXPECT_EQ(link.sent[1].payload, live::start_stream().payload);
}

TEST(LiveSession, AStartWithoutAnswerLeavesItJoined) {
    auto link = willing_camera();
    Session session(link);
    session.set_start_pause(std::chrono::milliseconds(0));
    ASSERT_TRUE(session.enter_live_mode());
    ASSERT_TRUE(session.join("HomeWiFi", "secret123"));
    link.answers.erase({0x02, 0x8E});
    EXPECT_FALSE(session.start_stream(kStream));
    EXPECT_EQ(session.state(), State::Joined);
}

TEST(LiveSession, LeavingAStreamStopsItThenReturnsToVideoMode) {
    auto link = willing_camera();
    Session session(link);
    session.set_start_pause(std::chrono::milliseconds(0));
    session.enter_live_mode();
    session.join("HomeWiFi", "secret123");
    session.start_stream(kStream);
    link.sent.clear();
    session.leave();
    EXPECT_EQ(session.state(), State::Idle);
    ASSERT_EQ(link.sent.size(), 2u);
    EXPECT_EQ(link.sent[0].payload, live::stop_stream().payload);
    EXPECT_EQ(link.sent[1].payload, live::video_mode().payload);
}

TEST(LiveSession, LeavingWithoutAStreamOnlyReturnsToVideoMode) {
    auto link = willing_camera();
    Session session(link);
    session.set_start_pause(std::chrono::milliseconds(0));
    session.leave();  // idle: nothing to do
    EXPECT_TRUE(link.sent.empty());
    session.enter_live_mode();
    session.join("HomeWiFi", "secret123");
    link.sent.clear();
    session.leave();
    ASSERT_EQ(link.sent.size(), 1u);
    EXPECT_EQ(link.sent[0].payload, live::video_mode().payload);
}

TEST(LiveSession, KeepAliveEveryIntervalFromLiveStreamingModeOn) {
    auto link = willing_camera();
    Session session(link);
    session.set_start_pause(std::chrono::milliseconds(0));
    const auto start = Session::Clock::now();
    session.keep_alive(start + std::chrono::seconds(10));  // idle: nothing
    EXPECT_TRUE(link.sent.empty());
    session.enter_live_mode();  // waiting for a network choice: the link must live on
    link.sent.clear();
    const auto entered = Session::Clock::now();
    session.keep_alive(entered + std::chrono::milliseconds(1000));  // not due yet
    EXPECT_TRUE(link.sent.empty());
    session.keep_alive(entered + live::kKeepAliveInterval);
    session.keep_alive(entered + live::kKeepAliveInterval + std::chrono::milliseconds(100));  // just sent
    ASSERT_EQ(link.sent.size(), 1u);
    EXPECT_EQ(link.sent[0].payload, live::keep_alive().payload);
}

TEST(LiveSession, ScanReturnsTheCamerasList) {
    auto link = willing_camera();
    duml::Frame list;
    list.cmd_set = 0x07;
    list.cmd_id = 0xAC;
    list.payload = {0x01, 0x11, 0x00, 0x00, 0x0A, 0x01, 0x01, 0x01, 0x00, 0x00, 'H', 'o', 'm', 'e'};
    link.pushes.push_back(list);
    Session session(link);
    session.set_start_pause(std::chrono::milliseconds(0));
    EXPECT_TRUE(session.scan(std::chrono::seconds(1)).empty());  // needs Live Streaming mode
    session.enter_live_mode();
    const auto networks = session.scan(std::chrono::seconds(1));
    ASSERT_EQ(networks.size(), 1u);
    EXPECT_EQ(networks[0].ssid, "Home");
}

TEST(LiveSession, RefusedLiveModeStaysIdle) {
    auto link = willing_camera();
    link.answers[{0x02, 0xE1}] = {0x01};
    Session session(link);
    session.set_start_pause(std::chrono::milliseconds(0));
    EXPECT_FALSE(session.enter_live_mode());
    EXPECT_EQ(session.state(), State::Idle);
}
