// DUML codec against frames captured from DJI Mimo and published by osmosis
// (same vectors as tools/test_duml.py).
#include "djivcam/duml.h"

#include <gtest/gtest.h>

#include <string>

namespace djivcam::duml {
namespace {

Bytes from_hex(const std::string& hex) {
    Bytes out;
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        out.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

const std::string kPairingFrame =
    "553304c202079280400745203238346165356238643736623333373561303461363431376164373162656133046f736d6fa0b4";

TEST(Duml, RoundTripsKnownFrames) {
    for (const std::string& hex : {std::string("550f04a202f01bcb40002b04009ab9"),       // 00/2b (Mimo)
                                   std::string("55110492021c1dcb40531000000000894a"),   // 53/10 (Mimo)
                                   std::string("550d043302070780400707fbcd"),           // 07/07
                                   std::string("550d043302070e8040070e5e01"), kPairingFrame}) {
        const Bytes raw = from_hex(hex);
        const auto frame = decode(raw);
        ASSERT_TRUE(frame) << hex;
        EXPECT_EQ(frame->encode(), raw) << hex;
    }
}

TEST(Duml, BuildsGetSsid) {
    const Frame frame{kAddrApp, kAddrWifi, 0x8007, kFlagRequest, 0x07, 0x07, {}};
    EXPECT_EQ(frame.encode(), from_hex("550d043302070780400707fbcd"));
}

TEST(Duml, BuildsPairing) {
    Bytes payload = pack_string("284ae5b8d76b3375a04a6417ad71bea3");
    const Bytes token = pack_string("osmo");
    payload.insert(payload.end(), token.begin(), token.end());
    const Frame frame{kAddrApp, kAddrWifi, 0x8092, kFlagRequest, 0x07, 0x45, payload};
    EXPECT_EQ(frame.encode(), from_hex(kPairingFrame));
}

TEST(Duml, ReplySwapsAddresses) {
    const Frame request{kAddrWifi, kAddrApp, 0x1234, kFlagRequest, 0x07, 0x46, {0x01}};
    EXPECT_TRUE(request.is_request());
    EXPECT_EQ(request.reply({0x01}).encode(), from_hex("550e046602073412c0074601a45c"));
}

TEST(Duml, RejectsCorruptFrames) {
    Bytes raw = from_hex("550d043302070780400707fbcd");
    raw[9] ^= 0x01;
    EXPECT_FALSE(decode(raw));
}

TEST(Duml, StreamParserHandlesSplitsAndJunk) {
    const Bytes a = from_hex("550d043302070780400707fbcd");
    const Bytes b = from_hex("550d043302070e8040070e5e01");
    Bytes stream = {0x00, 0x13};
    stream.insert(stream.end(), a.begin(), a.end());
    stream.insert(stream.end(), b.begin(), b.end());
    StreamParser parser;
    std::vector<Frame> frames;
    for (auto [from, to] : {std::pair{0, 7}, std::pair{7, 20}, std::pair{20, static_cast<int>(stream.size())}}) {
        auto got = parser.feed(std::span(stream).subspan(from, to - from));
        frames.insert(frames.end(), got.begin(), got.end());
    }
    ASSERT_EQ(frames.size(), 2U);
    EXPECT_EQ(frames[0].cmd_id, 0x07);
    EXPECT_EQ(frames[1].cmd_id, 0x0E);
}

}  // namespace
}  // namespace djivcam::duml
