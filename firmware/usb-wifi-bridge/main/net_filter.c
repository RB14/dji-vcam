#include "net_filter.h"

#include <string.h>

#define ETH_HDR_LEN        14
#define ETHERTYPE_IPV4     0x0800
#define ETHERTYPE_IPV6     0x86DD
#define IP_PROTO_UDP       17
#define IP6_NEXT_ICMPV6    58
#define IP6_HDR_LEN        40
#define ICMPV6_ROUTER_ADV  134
#define DHCP_SERVER_PORT   67
#define DHCP_CLIENT_PORT   68
#define BOOTP_FIXED_LEN    236
#define DHCP_MAGIC_LEN     4

#define DHCP_OPT_PAD            0
#define DHCP_OPT_ROUTER         3
#define DHCP_OPT_DNS            6
#define DHCP_OPT_CLASSLESS      121
#define DHCP_OPT_MS_CLASSLESS   249
#define DHCP_OPT_END            255

static const uint8_t DHCP_MAGIC[DHCP_MAGIC_LEN] = {0x63, 0x82, 0x53, 0x63};

static uint16_t read_be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static bool is_route_option(uint8_t code)
{
    return code == DHCP_OPT_ROUTER || code == DHCP_OPT_DNS ||
           code == DHCP_OPT_CLASSLESS || code == DHCP_OPT_MS_CLASSLESS;
}

/* Blanks route-related options in a DHCP payload. Returns true if anything changed. */
static bool scrub_dhcp_options(uint8_t *dhcp, size_t len)
{
    if (len < BOOTP_FIXED_LEN + DHCP_MAGIC_LEN ||
            memcmp(dhcp + BOOTP_FIXED_LEN, DHCP_MAGIC, DHCP_MAGIC_LEN) != 0) {
        return false;
    }
    bool changed = false;
    size_t i = BOOTP_FIXED_LEN + DHCP_MAGIC_LEN;
    while (i < len) {
        uint8_t code = dhcp[i];
        if (code == DHCP_OPT_END) {
            break;
        }
        if (code == DHCP_OPT_PAD) {
            i++;
            continue;
        }
        if (i + 1 >= len) {
            break;
        }
        size_t opt_len = 2 + dhcp[i + 1];
        if (i + opt_len > len) {
            break;
        }
        if (is_route_option(code)) {
            memset(dhcp + i, DHCP_OPT_PAD, opt_len);
            changed = true;
        }
        i += opt_len;
    }
    return changed;
}

static net_filter_verdict_t filter_ipv4(uint8_t *ip, size_t len)
{
    if (len < 20 || (ip[0] >> 4) != 4) {
        return NET_FILTER_PASS;
    }
    size_t ihl = (size_t)(ip[0] & 0x0F) * 4;
    if (ihl < 20 || len < ihl + 8 || ip[9] != IP_PROTO_UDP) {
        return NET_FILTER_PASS;
    }
    uint8_t *udp = ip + ihl;
    if (read_be16(udp) != DHCP_SERVER_PORT || read_be16(udp + 2) != DHCP_CLIENT_PORT) {
        return NET_FILTER_PASS;
    }
    if (!scrub_dhcp_options(udp + 8, len - ihl - 8)) {
        return NET_FILTER_PASS;
    }
    /* The payload changed but its length did not: a zero UDP checksum means "not computed". */
    udp[6] = 0;
    udp[7] = 0;
    return NET_FILTER_MODIFIED;
}

static net_filter_verdict_t filter_ipv6(const uint8_t *ip, size_t len)
{
    if (len > IP6_HDR_LEN && ip[6] == IP6_NEXT_ICMPV6 && ip[IP6_HDR_LEN] == ICMPV6_ROUTER_ADV) {
        return NET_FILTER_DROP;
    }
    return NET_FILTER_PASS;
}

net_filter_verdict_t net_filter_to_host(uint8_t *frame, size_t len)
{
    if (len <= ETH_HDR_LEN) {
        return NET_FILTER_PASS;
    }
    switch (read_be16(frame + 12)) {
    case ETHERTYPE_IPV4:
        return filter_ipv4(frame + ETH_HDR_LEN, len - ETH_HDR_LEN);
    case ETHERTYPE_IPV6:
        return filter_ipv6(frame + ETH_HDR_LEN, len - ETH_HDR_LEN);
    default:
        return NET_FILTER_PASS;
    }
}
