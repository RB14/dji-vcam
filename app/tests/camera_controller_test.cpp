#include "djivcam/camera_controller.h"

#include <gtest/gtest.h>

#include <deque>

namespace djivcam::camera {
namespace {

// Stands in for the live-view session: records requests and lets the test answer them.
class CameraControllerTest : public ::testing::Test {
protected:
    struct Sent {
        Command command;
        RequestTracker::ReplyCallback on_reply;
    };

    CameraControllerTest()
        : controller([this](const Command& command, RequestTracker::ReplyCallback on_reply) {
                         sent.push_back({command, std::move(on_reply)});
                     },
                     [this] { return streaming; }, [this] { ++changes; },
                     [this](const std::string& error) { errors.push_back(error); }) {}

    // Answers the oldest unanswered request with `payload` (nullopt = no answer).
    void answer(std::optional<Bytes> payload) {
        ASSERT_LT(answered, sent.size());
        Sent& request = sent[answered++];
        if (payload) {
            request.on_reply(duml::Frame{request.command.receiver, duml::kAddrApp, 1, duml::kFlagResponse,
                                         request.command.cmd_set, request.command.cmd_id, *payload});
        } else {
            request.on_reply(std::nullopt);
        }
    }
    // Answers every request (subscriptions and reads after connecting) with success.
    void answer_all_ok() {
        while (answered < sent.size()) {
            answer(Bytes{0x00});
        }
    }
    std::size_t unanswered() const { return sent.size() - answered; }

    bool streaming = true;
    int changes = 0;
    std::vector<std::string> errors;
    std::deque<Sent> sent;  // answering sends the next request: element references must stay valid
    std::size_t answered = 0;
    CameraController controller;
};

TEST_F(CameraControllerTest, SubscribesAndReadsParametersOneRequestAtATime) {
    controller.on_streaming();
    EXPECT_EQ(sent.size(), 1u);  // one in flight
    EXPECT_EQ(sent[0].command.cmd_id, 0x99);
    answer_all_ok();
    EXPECT_EQ(sent.size(), status_topics().size() + 3);  // + stabilization, FOV and ISO-limit reads
    EXPECT_EQ(sent.back().command.cmd_id, 0x8E);
    EXPECT_TRUE(errors.empty());  // background requests fail quietly
}

TEST_F(CameraControllerTest, NewerChangeOfTheSameSettingReplacesTheQueuedOne) {
    controller.set(Setting::Ev, 17);  // sent right away
    controller.set(Setting::Ev, 18);  // queued
    controller.set(Setting::Ev, 19);  // replaces 18
    ASSERT_EQ(sent.size(), 1u);
    answer(Bytes{0x00});
    ASSERT_EQ(sent.size(), 2u);
    EXPECT_EQ(sent[1].command.payload, (Bytes{19}));
    answer(Bytes{0x00});
    EXPECT_EQ(unanswered(), 0u);
}

TEST_F(CameraControllerTest, ReportsRefusalsInWords) {
    controller.set(Setting::Color, 0x3D);
    answer(Bytes{0xD9});
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0], "Color D-Log M (10-bit): not possible right now (recording, or not in this mode)");
    controller.record(true);
    answer(std::nullopt);
    ASSERT_EQ(errors.size(), 2u);
    EXPECT_EQ(errors[1], "the camera did not answer (start recording)");
}

TEST_F(CameraControllerTest, ReadsAParameterBackAfterSettingIt) {
    controller.set(Setting::Stabilization, 0x03);
    answer(Bytes{0x00});
    ASSERT_EQ(sent.size(), 2u);
    EXPECT_EQ(sent[1].command.payload, (Bytes{0x00, 0x01, 0x08, 0x00}));  // GET pid 0x0008
    answer(Bytes{0x00, 0x00, 0x01, 0x08, 0x00, 0x01, 0x03});
    EXPECT_EQ(controller.state().values.at(Setting::Stabilization), 0x03);
    EXPECT_GE(changes, 1);
}

TEST_F(CameraControllerTest, DropsRequestsWhileNotStreaming) {
    streaming = false;
    controller.set(Setting::Iso, 5);
    EXPECT_TRUE(sent.empty());
    streaming = true;
    controller.set(Setting::Iso, 6);
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].command.payload, (Bytes{6}));
}

TEST_F(CameraControllerTest, FoldsStatusPushesIntoTheState) {
    Bytes payload = {0x02, 0x06, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1A, 0x00, 0x0A, 0x00};
    const std::string topic = "cam_status";
    payload.insert(payload.end(), topic.begin(), topic.end());
    payload.insert(payload.end(), 6, 0x00);
    payload.insert(payload.end(), {0x05, 0x00, 0x18, 0x00, 0x5F, 0x01, 0x05});
    controller.on_message(duml::Frame{0x28, duml::kAddrApp, 1, duml::kFlagPush, 0x00, 0x99, payload});
    const auto state = controller.state();
    EXPECT_TRUE(state.recording);
    EXPECT_EQ(state.values.at(Setting::Mode), 0x05);
    EXPECT_EQ(changes, 1);
}

}  // namespace
}  // namespace djivcam::camera
