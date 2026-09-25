#include "djivcam/wifi_channel.h"

#include <gtest/gtest.h>

using djivcam::wifi::interference;
using djivcam::wifi::Network;
using djivcam::wifi::quietest_channel;

TEST(WifiChannel, InterferenceFallsWithChannelDistance) {
    const std::vector<Network> networks = {{6, -50, "a"}};
    EXPECT_GT(interference(networks, 6), interference(networks, 7));
    EXPECT_GT(interference(networks, 7), interference(networks, 9));
    EXPECT_EQ(interference(networks, 11), 0.0);  // 5 channels away: no overlap
    EXPECT_EQ(interference(networks, 1), 0.0);
}

TEST(WifiChannel, MovesAwayFromACrowdedChannel) {
    // The evening of 2026-09-25: the camera on 10, strong neighbours on 9, 11 and 13, channel 1 quiet.
    const std::vector<Network> networks = {{4, -59, "net-a"}, {13, -62, ""},     {9, -66, "net-b"},
                                           {1, -73, "net-c"}, {9, -74, "net-d"}, {11, -76, "net-e"},
                                           {10, -30, "osmo5-cam"}};
    EXPECT_EQ(quietest_channel(networks, "osmo5-cam", 10), 1);
}

TEST(WifiChannel, IgnoresTheCamerasOwnNetwork) {
    const std::vector<Network> networks = {{1, -30, "osmo5-cam"}, {6, -80, "far"}, {11, -80, "far too"}};
    EXPECT_EQ(quietest_channel(networks, "osmo5-cam", 1), 0);  // 1 is the quietest once the camera is left out
}

TEST(WifiChannel, StaysWhenTheGainIsSmall) {
    const std::vector<Network> networks = {{1, -70, "a"}, {6, -70, "b"}, {11, -70, "c"}};
    EXPECT_EQ(quietest_channel(networks, "cam", 6), 0);
}

TEST(WifiChannel, NeedsAClearGainBetweenGridChannels) {
    // Scan-to-scan noise of a couple of dB must not move the camera back and forth (off the grid,
    // 1.3 times is enough: MovesAwayFromACrowdedChannel)
    EXPECT_EQ(quietest_channel({{1, -72, "a"}, {6, -70, "b"}, {11, -70, "c"}}, "cam", 6), 0);
    EXPECT_EQ(quietest_channel({{1, -74, "a"}, {6, -70, "b"}, {11, -70, "c"}}, "cam", 6), 1);  // 4 dB
}

TEST(WifiChannel, UnknownCurrentChannelTakesTheQuietest) {
    const std::vector<Network> networks = {{1, -50, "a"}, {6, -80, "b"}, {11, -55, "c"}};
    EXPECT_EQ(quietest_channel(networks, "cam", 0), 6);
}

TEST(WifiChannel, EmptyAirStaysPut) {
    EXPECT_EQ(quietest_channel({}, "cam", 3), 0);  // nothing to gain anywhere
}
