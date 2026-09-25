#include "djivcam/reassembler.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace djivcam {
namespace {

using namespace std::chrono_literals;
using Clock = VideoReassembler::Clock;

class ReassemblerTest : public ::testing::Test {
protected:
    // Each datagram carries its own sequence number as a single byte, so the delivery order is
    // easy to check.
    void push(std::uint16_t seq, Clock::time_point at) {
        const std::uint8_t tag = static_cast<std::uint8_t>(seq / VideoReassembler::kSeqStep);
        reassembler.push(seq, std::span(&tag, 1), at);
    }

    std::vector<int> delivered;
    Clock::time_point t0 = Clock::now();
    VideoReassembler reassembler{[this](std::span<const std::uint8_t> p) { delivered.push_back(p[0]); }, 50ms};
};

TEST_F(ReassemblerTest, DeliversInOrderStreamAndAcksLast) {
    for (int i = 1; i <= 3; ++i) {
        push(static_cast<std::uint16_t>(i * 8), t0);
    }
    EXPECT_EQ(delivered, (std::vector<int>{1, 2, 3}));
    EXPECT_EQ(reassembler.ack_seq(), 24);
}

TEST_F(ReassemblerTest, HoldsDatagramsBehindAGapUntilItIsFilled) {
    push(8, t0);
    push(24, t0);  // 16 missing
    push(32, t0);
    EXPECT_EQ(delivered, (std::vector<int>{1}));
    EXPECT_EQ(reassembler.ack_seq(), 8);  // tells the camera where the gap is
    push(16, t0 + 10ms);                  // re-sent / late
    EXPECT_EQ(delivered, (std::vector<int>{1, 2, 3, 4}));
    EXPECT_EQ(reassembler.ack_seq(), 32);
    EXPECT_EQ(reassembler.stats().recovered, 1U);
    EXPECT_EQ(reassembler.stats().skipped, 0U);
}

TEST_F(ReassemblerTest, SkipsAGapThatIsNeverFilled) {
    push(8, t0);
    push(32, t0);  // 16 and 24 missing
    reassembler.poll(t0 + 20ms);
    EXPECT_EQ(delivered, (std::vector<int>{1}));
    reassembler.poll(t0 + 60ms);
    EXPECT_EQ(delivered, (std::vector<int>{1, 4}));
    EXPECT_EQ(reassembler.ack_seq(), 32);
    EXPECT_EQ(reassembler.stats().skipped, 2U);
    push(16, t0 + 70ms);  // too late now
    EXPECT_EQ(reassembler.stats().duplicates, 1U);
}

TEST_F(ReassemblerTest, HandlesWrapAround) {
    push(0xFFF0, t0);
    push(0x0000, t0);  // 0xFFF8 missing
    push(0xFFF8, t0);
    EXPECT_EQ(delivered.size(), 3U);
    EXPECT_EQ(reassembler.ack_seq(), 0x0000);
}

TEST_F(ReassemblerTest, FarJumpIsAStreamRestart) {
    push(8, t0);
    push(0x8008, t0);
    EXPECT_EQ(delivered.size(), 2U);
    EXPECT_EQ(reassembler.stats().restarts, 1U);
    EXPECT_EQ(reassembler.ack_seq(), 0x8008);
}

TEST(VideoReassemblerSkip, ReportsAGapBeforeDeliveringWhatFollowsIt) {
    std::vector<std::string> events;
    VideoReassembler reassembler(
        [&](std::span<const std::uint8_t> p) { events.push_back("deliver " + std::to_string(p[0])); }, 0ms,
        [&](std::uint64_t skipped) { events.push_back("skip " + std::to_string(skipped)); });
    const auto t0 = VideoReassembler::Clock::now();
    const std::uint8_t one = 1, three = 3;
    reassembler.push(8, std::span(&one, 1), t0);
    reassembler.push(24, std::span(&three, 1), t0);  // 16 lost
    reassembler.poll(t0);
    EXPECT_EQ(events, (std::vector<std::string>{"deliver 1", "skip 1", "deliver 3"}));
}

}  // namespace
}  // namespace djivcam
