#include "osmolink/h264.h"

#include <gtest/gtest.h>

namespace osmolink::h264 {
namespace {

// One NAL unit with a 4-byte start code.
Bytes nal(std::initializer_list<std::uint8_t> body) {
    Bytes out = {0x00, 0x00, 0x00, 0x01};
    out.insert(out.end(), body);
    return out;
}

Bytes concat(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const Bytes& part : parts) {
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

TEST(AccessUnitAssembler, SplitsAtAudDropsDjiUnitsAndFlagsKeyframes) {
    const Bytes aud = nal({0x09, 0x10});
    const Bytes dji = nal({0xFF, 0x9A, 0x06});
    const Bytes sps = nal({0x67, 0x64, 0x00, 0x20});
    const Bytes idr = nal({0x65, 0xB8, 0x20});
    const Bytes slice = nal({0x41, 0xF2, 0x22});
    // A NAL unit is complete only once the next start code arrives, so the trailing DJI unit is
    // what closes the third AUD, which in turn closes the second access unit.
    const Bytes stream = concat({aud, dji, sps, idr, aud, dji, slice, aud, dji});

    std::vector<AccessUnit> units;
    AccessUnitAssembler assembler([&](AccessUnit&& unit) { units.push_back(std::move(unit)); });
    for (std::size_t i = 0; i < stream.size(); i += 3) {  // arbitrary chunking, like datagrams
        assembler.push(std::span(stream).subspan(i, std::min<std::size_t>(3, stream.size() - i)));
    }

    ASSERT_EQ(units.size(), 2U);
    EXPECT_EQ(units[0].data, concat({aud, sps, idr}));
    EXPECT_TRUE(units[0].keyframe);
    EXPECT_EQ(units[1].data, concat({aud, slice}));
    EXPECT_FALSE(units[1].keyframe);
}

}  // namespace
}  // namespace osmolink::h264
