#include "djivcam/net.h"

#include <cstddef>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace djivcam::net {
namespace {

void ensure_sockets_initialized() {
#ifdef _WIN32
    static const bool initialized = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    (void)initialized;
#endif
}

void close_socket(SocketHandle handle) {
#ifdef _WIN32
    closesocket(handle);
#else
    ::close(handle);
#endif
}

std::optional<sockaddr_in> make_address(const std::string& ip, std::uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (ip.empty()) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        return std::nullopt;
    }
    return addr;
}

// Waits until `handle` is readable (or writable) for at most `timeout`.
bool wait_ready(SocketHandle handle, bool for_write, std::chrono::milliseconds timeout) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(handle, &set);
    timeval tv{};
    tv.tv_sec = static_cast<long>(timeout.count() / 1000);
    tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
    const int nfds = static_cast<int>(handle) + 1;
    const int ready = for_write ? select(nfds, nullptr, &set, nullptr, &tv) : select(nfds, &set, nullptr, nullptr, &tv);
    return ready > 0;
}

void set_blocking(SocketHandle handle, bool blocking) {
#ifdef _WIN32
    u_long mode = blocking ? 0 : 1;
    ioctlsocket(handle, FIONBIO, &mode);
#else
    const int flags = fcntl(handle, F_GETFL, 0);
    fcntl(handle, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK));
#endif
}

}  // namespace

std::optional<std::string> local_ip_towards(const std::string& ip, std::uint16_t port) {
    ensure_sockets_initialized();
    const auto remote = make_address(ip, port);
    if (!remote) {
        return std::nullopt;
    }
    const SocketHandle probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (probe == kInvalidSocket) {
        return std::nullopt;
    }
    std::optional<std::string> result;
    sockaddr_in local{};
    socklen_t len = sizeof(local);
    if (connect(probe, reinterpret_cast<const sockaddr*>(&*remote), sizeof(*remote)) == 0 &&
        getsockname(probe, reinterpret_cast<sockaddr*>(&local), &len) == 0) {
        char text[INET_ADDRSTRLEN] = {};
        if (inet_ntop(AF_INET, &local.sin_addr, text, sizeof(text))) {
            result = text;
        }
    }
    close_socket(probe);
    return result;
}

bool tcp_send_once(const std::string& ip, std::uint16_t port, std::span<const std::uint8_t> data,
                   std::chrono::milliseconds connect_timeout, std::chrono::milliseconds linger) {
    ensure_sockets_initialized();
    const auto remote = make_address(ip, port);
    const SocketHandle handle = remote ? socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) : kInvalidSocket;
    if (handle == kInvalidSocket) {
        return false;
    }
    set_blocking(handle, false);
    connect(handle, reinterpret_cast<const sockaddr*>(&*remote), sizeof(*remote));
    bool ok = wait_ready(handle, true, connect_timeout);
    if (ok) {
        int error = 0;
        socklen_t len = sizeof(error);
        getsockopt(handle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &len);
        ok = error == 0;
    }
    if (ok) {
        set_blocking(handle, true);
        ok = send(handle, reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()), 0) ==
             static_cast<int>(data.size());
        std::this_thread::sleep_for(linger);
    }
    close_socket(handle);
    return ok;
}

std::optional<UdpSocket> UdpSocket::open(const std::string& local_ip) {
    ensure_sockets_initialized();
    const auto local = make_address(local_ip, 0);
    const SocketHandle handle = local ? socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP) : kInvalidSocket;
    if (handle == kInvalidSocket) {
        return std::nullopt;
    }
    const int receive_buffer = 4 * 1024 * 1024;
    setsockopt(handle, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&receive_buffer), sizeof(receive_buffer));
    if (bind(handle, reinterpret_cast<const sockaddr*>(&*local), sizeof(*local)) != 0) {
        close_socket(handle);
        return std::nullopt;
    }
    return UdpSocket(static_cast<std::intptr_t>(handle));
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept : handle_(other.handle_) { other.handle_ = -1; }

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        other.handle_ = -1;
    }
    return *this;
}

UdpSocket::~UdpSocket() { close(); }

void UdpSocket::close() {
    if (handle_ != -1) {
        close_socket(static_cast<SocketHandle>(handle_));
        handle_ = -1;
    }
}

bool UdpSocket::send_to(const std::string& ip, std::uint16_t port, std::span<const std::uint8_t> data) {
    const auto remote = make_address(ip, port);
    if (!remote) {
        return false;
    }
    const auto sent = sendto(static_cast<SocketHandle>(handle_), reinterpret_cast<const char*>(data.data()),
                             static_cast<int>(data.size()), 0, reinterpret_cast<const sockaddr*>(&*remote),
                             sizeof(*remote));
    return static_cast<std::ptrdiff_t>(sent) == static_cast<std::ptrdiff_t>(data.size());
}

std::optional<std::size_t> UdpSocket::receive(std::span<std::uint8_t> buffer, std::chrono::milliseconds timeout) {
    const auto handle = static_cast<SocketHandle>(handle_);
    if (!wait_ready(handle, false, timeout)) {
        return std::nullopt;
    }
    const auto got = recv(handle, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0);
    if (got < 0) {
        return std::nullopt;  // e.g. ICMP port unreachable surfacing as WSAECONNRESET
    }
    return static_cast<std::size_t>(got);
}

}  // namespace djivcam::net
