// Minimal cross-platform (Winsock / POSIX) UDP and TCP helpers for the camera datalink.
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace djivcam::net {

// Local IPv4 address the OS would use to reach `ip` (no packets are sent).
std::optional<std::string> local_ip_towards(const std::string& ip, std::uint16_t port);

// An IPv4 network this computer is on (an up, non-loopback interface).
struct LocalNetwork {
    std::string ip;         // this computer's address on it
    int prefix_length = 0;  // 24 for 255.255.255.0
};
std::vector<LocalNetwork> local_networks();

// The addresses find_ip_by_mac() nudges on `network`: every host of its /24 (or of the network if it
// is smaller), except this computer.
std::vector<std::string> sweep_targets(const LocalNetwork& network);

// "58:b8:58:00:00:01" from "58-B8-58-00-00-01", "58:B8:58:00:00:01" or "58b858000001"; empty if it is
// not a MAC.
std::string normalize_mac(std::string_view mac);

// The IPv4 address of the device with this MAC on a network this computer is on (e.g. the camera
// after it joined the Wi-Fi): sends every address of each local network a small UDP datagram, which
// makes the OS resolve it, and looks the MAC up in the neighbour table. nullopt within `timeout`.
std::optional<std::string> find_ip_by_mac(std::string_view mac, std::chrono::milliseconds timeout);

// Connects to ip:port, sends `data`, keeps the connection for `linger`, then closes it.
bool tcp_send_once(const std::string& ip, std::uint16_t port, std::span<const std::uint8_t> data,
                   std::chrono::milliseconds connect_timeout, std::chrono::milliseconds linger);

class UdpSocket {
public:
    // Binds to local_ip (empty = any) on an ephemeral port.
    static std::optional<UdpSocket> open(const std::string& local_ip);

    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    ~UdpSocket();

    bool send_to(const std::string& ip, std::uint16_t port, std::span<const std::uint8_t> data);
    // Receives one datagram into `buffer`, waiting up to `timeout`. Returns its size.
    std::optional<std::size_t> receive(std::span<std::uint8_t> buffer, std::chrono::milliseconds timeout);

private:
    explicit UdpSocket(std::intptr_t handle) : handle_(handle) {}
    void close();

    std::intptr_t handle_ = -1;
};

}  // namespace djivcam::net
