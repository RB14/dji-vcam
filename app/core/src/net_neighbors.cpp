#include "djivcam/net.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

#include <fstream>
#include <sstream>
#endif

namespace djivcam::net {
namespace {

using namespace std::chrono_literals;

constexpr std::uint16_t kDiscardPort = 9;
constexpr auto kPoll = 200ms;
constexpr auto kNudgeAgain = 1500ms;

std::uint32_t parse_ipv4(const std::string& ip) {
    in_addr address{};
    return inet_pton(AF_INET, ip.c_str(), &address) == 1 ? ntohl(address.s_addr) : 0;
}

std::string format_ipv4(std::uint32_t ip) {
    return std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 0xFF) + "." + std::to_string((ip >> 8) & 0xFF) +
           "." + std::to_string(ip & 0xFF);
}

#ifdef _WIN32
std::string format_mac(const unsigned char* bytes) {
    char text[18];
    std::snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x", bytes[0], bytes[1], bytes[2], bytes[3], bytes[4],
                  bytes[5]);
    return text;
}
#endif

// The neighbour table's resolved entries: IP of the given MAC, if any.
std::optional<std::string> lookup(const std::string& mac) {
#ifdef _WIN32
    PMIB_IPNET_TABLE2 table = nullptr;
    if (GetIpNetTable2(AF_INET, &table) != NO_ERROR) {
        return std::nullopt;
    }
    std::optional<std::string> found;
    for (ULONG i = 0; i < table->NumEntries && !found; ++i) {
        const MIB_IPNET_ROW2& row = table->Table[i];
        const bool resolved = row.State == NlnsReachable || row.State == NlnsStale || row.State == NlnsDelay ||
                              row.State == NlnsProbe || row.State == NlnsPermanent;
        if (resolved && row.PhysicalAddressLength == 6 && format_mac(row.PhysicalAddress) == mac) {
            found = format_ipv4(ntohl(row.Address.Ipv4.sin_addr.s_addr));
        }
    }
    FreeMibTable(table);
    return found;
#else
    std::ifstream arp("/proc/net/arp");
    std::string line;
    std::getline(arp, line);  // header
    while (std::getline(arp, line)) {
        std::istringstream fields(line);
        std::string ip, type, flags, hw;
        fields >> ip >> type >> flags >> hw;
        if (flags != "0x0" && normalize_mac(hw) == mac) {
            return ip;
        }
    }
    return std::nullopt;
#endif
}

}  // namespace

std::vector<LocalNetwork> local_networks() {
    std::vector<LocalNetwork> networks;
#ifdef _WIN32
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buffer;
    ULONG result = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 3 && result == ERROR_BUFFER_OVERFLOW; ++attempt) {
        buffer.resize(size);
        result = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                      nullptr, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()), &size);
    }
    if (result != NO_ERROR) {
        return networks;
    }
    for (auto* adapter = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()); adapter; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
            continue;
        }
        for (auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
            const auto* address = reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
            networks.push_back({format_ipv4(ntohl(address->sin_addr.s_addr)), unicast->OnLinkPrefixLength});
        }
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) {
        return networks;
    }
    for (ifaddrs* item = list; item; item = item->ifa_next) {
        if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET || (item->ifa_flags & IFF_LOOPBACK) ||
            !(item->ifa_flags & IFF_UP) || !item->ifa_netmask) {
            continue;
        }
        const auto ip = ntohl(reinterpret_cast<const sockaddr_in*>(item->ifa_addr)->sin_addr.s_addr);
        const auto mask = ntohl(reinterpret_cast<const sockaddr_in*>(item->ifa_netmask)->sin_addr.s_addr);
        int prefix = 0;
        for (auto bits = mask; bits & 0x80000000u; bits <<= 1) {
            ++prefix;
        }
        networks.push_back({format_ipv4(ip), prefix});
    }
    freeifaddrs(list);
#endif
    return networks;
}

std::vector<std::string> sweep_targets(const LocalNetwork& network) {
    const std::uint32_t ip = parse_ipv4(network.ip);
    if (ip == 0 || network.prefix_length <= 0 || network.prefix_length >= 31) {
        return {};
    }
    const int prefix = network.prefix_length < 24 ? 24 : network.prefix_length;  // at most 254 hosts
    const std::uint32_t mask = 0xFFFFFFFFu << (32 - prefix);
    const std::uint32_t first = (ip & mask) + 1;
    const std::uint32_t last = (ip | ~mask) - 1;  // before the broadcast address
    std::vector<std::string> targets;
    for (std::uint32_t host = first; host <= last; ++host) {
        if (host != ip) {
            targets.push_back(format_ipv4(host));
        }
    }
    return targets;
}

std::string normalize_mac(std::string_view mac) {
    std::string digits;
    for (char c : mac) {
        if (std::isxdigit(static_cast<unsigned char>(c))) {
            digits += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (c != ':' && c != '-') {
            return {};
        }
    }
    if (digits.size() != 12) {
        return {};
    }
    std::string out;
    for (std::size_t i = 0; i < 12; i += 2) {
        out += (i ? ":" : "") + digits.substr(i, 2);
    }
    return out;
}

std::optional<std::string> find_ip_by_mac(std::string_view mac_text, std::chrono::milliseconds timeout,
                                          std::stop_token stop) {
    const std::string mac = normalize_mac(mac_text);
    if (mac.empty()) {
        return std::nullopt;
    }
    std::vector<std::string> targets;
    for (const LocalNetwork& network : local_networks()) {
        const auto more = sweep_targets(network);
        targets.insert(targets.end(), more.begin(), more.end());
    }
    auto socket = UdpSocket::open({});
    const std::array<std::uint8_t, 1> nudge = {0};
    const auto until = std::chrono::steady_clock::now() + timeout;
    auto next_nudge = std::chrono::steady_clock::now();
    while (true) {
        if (auto ip = lookup(mac)) {
            return ip;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= until || stop.stop_requested()) {
            return std::nullopt;
        }
        if (socket && now >= next_nudge) {  // makes the OS resolve every address, filling its table
            for (const std::string& target : targets) {
                socket->send_to(target, kDiscardPort, nudge);
            }
            next_nudge = now + kNudgeAgain;
        }
        std::this_thread::sleep_for(kPoll);
    }
}

}  // namespace djivcam::net
