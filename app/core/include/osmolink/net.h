// Minimal cross-platform (Winsock / POSIX) UDP and TCP helpers for the camera datalink.
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace osmolink::net {

// Local IPv4 address the OS would use to reach `ip` (no packets are sent).
std::optional<std::string> local_ip_towards(const std::string& ip, std::uint16_t port);

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

}  // namespace osmolink::net
