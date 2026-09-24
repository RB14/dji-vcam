#include "djivcam/request_tracker.h"

#include <gtest/gtest.h>

namespace djivcam {
namespace {

using namespace std::chrono_literals;
using Clock = RequestTracker::Clock;

duml::Frame request(std::uint16_t seq, std::uint8_t cmd_set, std::uint8_t cmd_id) {
    return duml::Frame{duml::kAddrApp, duml::kAddrCamera, seq, duml::kFlagRequest, cmd_set, cmd_id, {}};
}

class RequestTrackerTest : public ::testing::Test {
protected:
    RequestTracker::ReplyCallback record(int id) {
        return [this, id](std::optional<duml::Frame> reply) { results.emplace_back(id, std::move(reply)); };
    }

    RequestTracker tracker;
    std::vector<std::pair<int, std::optional<duml::Frame>>> results;
    Clock::time_point t0 = Clock::now();
};

TEST_F(RequestTrackerTest, MatchesReplyBySequenceAndCommand) {
    tracker.add(request(0x100, 0x02, 0x8E), record(1), t0 + 1s);
    tracker.add(request(0x101, 0x02, 0x8E), record(2), t0 + 1s);
    EXPECT_TRUE(tracker.resolve(request(0x101, 0x02, 0x8E).reply({0x00, 0x05})));
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].first, 2);
    ASSERT_TRUE(results[0].second);
    EXPECT_EQ(results[0].second->payload, (duml::Bytes{0x00, 0x05}));
    EXPECT_EQ(tracker.pending(), 1u);
}

TEST_F(RequestTrackerTest, IgnoresOtherTrafficRequestsAndEmptyReplies) {
    tracker.add(request(0x100, 0x02, 0x8E), record(1), t0 + 1s);
    EXPECT_FALSE(tracker.resolve(request(0x100, 0x02, 0x8F).reply({0x00})));  // other command
    EXPECT_FALSE(tracker.resolve(request(0x100, 0x02, 0x8E)));               // a request, not a reply
    EXPECT_FALSE(tracker.resolve(request(0x100, 0x02, 0x8E).reply({})));      // transport ACK
    duml::Frame push{duml::kAddrCamera, duml::kAddrApp, 0x100, duml::kFlagPush, 0x02, 0x8E, {0x00}};
    EXPECT_FALSE(tracker.resolve(push));  // a push that happens to share the sequence number
    duml::Frame stranger = request(0x100, 0x02, 0x8E).reply({0x00});
    stranger.sender = 0x28;
    EXPECT_FALSE(tracker.resolve(stranger));  // answered by another endpoint
    EXPECT_TRUE(results.empty());
    EXPECT_EQ(tracker.pending(), 1u);
}

TEST_F(RequestTrackerTest, ExpiresOnlyOverdueRequests) {
    tracker.add(request(1, 0x02, 0x01), record(1), t0 + 100ms);
    tracker.add(request(2, 0x02, 0x01), record(2), t0 + 500ms);
    tracker.expire(t0 + 200ms);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].first, 1);
    EXPECT_FALSE(results[0].second);
    EXPECT_EQ(tracker.pending(), 1u);
}

TEST_F(RequestTrackerTest, FailAllReportsEveryPendingRequest) {
    tracker.add(request(1, 0x02, 0x01), record(1), t0 + 1s);
    tracker.add(request(2, 0x02, 0x02), record(2), t0 + 1s);
    tracker.fail_all();
    EXPECT_EQ(results.size(), 2u);
    EXPECT_EQ(tracker.pending(), 0u);
}

TEST_F(RequestTrackerTest, CallbackMayQueueAFollowUpRequest) {
    tracker.add(request(1, 0x02, 0x01), [this](std::optional<duml::Frame>) {
        tracker.add(request(2, 0x02, 0x02), record(2), t0 + 1s);
    }, t0 + 1s);
    EXPECT_TRUE(tracker.resolve(request(1, 0x02, 0x01).reply({0x00})));
    EXPECT_EQ(tracker.pending(), 1u);
}

}  // namespace
}  // namespace djivcam
