#include "djivcam/net.h"

#include <gtest/gtest.h>

#include <algorithm>

using djivcam::net::LocalNetwork;
using djivcam::net::normalize_mac;
using djivcam::net::on_network;
using djivcam::net::same_network;
using djivcam::net::sweep_targets;

TEST(NetNeighbors, NormalizesMacs) {
    EXPECT_EQ(normalize_mac("58-B8-58-00-00-01"), "58:b8:58:00:00:01");
    EXPECT_EQ(normalize_mac("58:b8:58:00:00:01"), "58:b8:58:00:00:01");
    EXPECT_EQ(normalize_mac("58B858000001"), "58:b8:58:00:00:01");
    EXPECT_EQ(normalize_mac("58:b8:58:00:00"), "");
    EXPECT_EQ(normalize_mac("not a mac at all"), "");
}

TEST(NetNeighbors, TellsWhichAddressesAreOnANetwork) {
    EXPECT_TRUE(on_network(LocalNetwork{"192.168.1.10", 24}, "192.168.1.200"));
    EXPECT_FALSE(on_network(LocalNetwork{"192.168.1.10", 24}, "192.168.2.1"));  // e.g. an old access point
    EXPECT_TRUE(on_network(LocalNetwork{"10.0.5.20", 16}, "10.0.7.3"));         // wider than a /24
    EXPECT_FALSE(on_network(LocalNetwork{"10.0.5.20", 16}, "10.1.5.20"));
    EXPECT_TRUE(on_network(LocalNetwork{"192.168.1.10", 32}, "192.168.1.10"));
    EXPECT_FALSE(on_network(LocalNetwork{"192.168.1.10", 0}, "192.168.1.11"));
    EXPECT_FALSE(on_network(LocalNetwork{"bad", 24}, "192.168.1.11"));
}

TEST(NetNeighbors, LoopbackIsTheSameNetwork) {
    EXPECT_TRUE(same_network("127.0.0.1", "127.0.0.1"));
    EXPECT_FALSE(same_network("192.0.2.10", "198.51.100.1"));  // documentation ranges: never local here
}

TEST(NetNeighbors, SweepsTheSlash24WithoutThisComputer) {
    const auto targets = sweep_targets(LocalNetwork{"192.168.1.10", 24});
    ASSERT_EQ(targets.size(), 253u);  // .1 to .254, without .10
    EXPECT_EQ(targets.front(), "192.168.1.1");
    EXPECT_EQ(targets.back(), "192.168.1.254");
    EXPECT_EQ(std::count(targets.begin(), targets.end(), "192.168.1.10"), 0);
}

TEST(NetNeighbors, LargeNetworksAreSweptAroundThisComputer) {
    const auto targets = sweep_targets(LocalNetwork{"10.0.5.20", 16});
    ASSERT_EQ(targets.size(), 253u);
    EXPECT_EQ(targets.front(), "10.0.5.1");
}

TEST(NetNeighbors, SmallNetworksStayInside) {
    const auto targets = sweep_targets(LocalNetwork{"192.168.1.10", 29});  // .8 to .15
    EXPECT_EQ(targets, (std::vector<std::string>{"192.168.1.9", "192.168.1.11", "192.168.1.12", "192.168.1.13",
                                                 "192.168.1.14"}));
    EXPECT_TRUE(sweep_targets(LocalNetwork{"192.168.1.10", 32}).empty());
    EXPECT_TRUE(sweep_targets(LocalNetwork{"bad", 24}).empty());
}
