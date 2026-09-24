// UDP datalink framing against a command packet derived from osmosis
// (same vector as tools/test_datalink.py).
#include "djivcam/datalink.h"

#include <gtest/gtest.h>

namespace djivcam::datalink {
namespace {

std::string to_hex(const Bytes& bytes) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (std::uint8_t byte : bytes) {
        out += digits[byte >> 4];
        out += digits[byte & 0xF];
    }
    return out;
}

TEST(Datalink, CommandPacketMatchesReference) {
    const duml::Frame frame{duml::kAddrApp, duml::kAddrDm368First, 0xA000, duml::kFlagRequest, 0x00, 0x88,
                            {0x17, 0x00, 0x46, 0x23, 0x7c, 0x41, 0x50, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02}};
    Bytes body = routing_header(0x5A40, 1);
    const Bytes encoded = frame.encode();
    body.insert(body.end(), encoded.begin(), encoded.end());
    Bytes packet = header(PacketType::Command, body.size(), 0x4C21, 0x5A40);
    packet.insert(packet.end(), body.begin(), body.end());
    EXPECT_EQ(to_hex(packet),
              "2f80214c405a05dd385a405a0000000001010000"
              "551b0475022800a0400088170046237c415050000000000002e6e8");
}

TEST(Datalink, HeaderXor) { EXPECT_EQ(to_hex(header(PacketType::Handshake, 40, 0x4C21, 0)), "3080214c000000dd"); }

TEST(Datalink, SequenceComparisonsWrap) {
    EXPECT_TRUE(seq_ahead(0x0002, 0xFFF0));
    EXPECT_FALSE(seq_ahead(0xFFF0, 0x0002));
    EXPECT_FALSE(seq_ahead(0x1234, 0x1234));
    EXPECT_TRUE(seq_near(0x0002, 0xFFF0));
    EXPECT_FALSE(seq_near(0x8000, 0x0000));  // a stream restart, not a late packet
}

}  // namespace
}  // namespace djivcam::datalink
