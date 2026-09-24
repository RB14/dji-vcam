/*
 * Frame filters applied to traffic travelling from the camera's Wi-Fi network to the USB host.
 *
 * The bridge must never become the host's route to the internet: the laptop keeps using its own
 * Wi-Fi for that. We therefore scrub every hint of a default route that the camera's network
 * could hand out.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    NET_FILTER_PASS,      /* forward the frame unchanged */
    NET_FILTER_MODIFIED,  /* forward the frame, it was rewritten in place */
    NET_FILTER_DROP,      /* do not forward the frame */
} net_filter_verdict_t;

/*
 * Inspects an Ethernet frame heading to the USB host.
 * - DHCP server replies: router (3), DNS (6) and classless static route (121, 249) options are
 *   blanked out with PAD bytes, so the host only learns the camera subnet itself.
 * - IPv6 Router Advertisements are dropped, so the host never gains an IPv6 default route here.
 */
net_filter_verdict_t net_filter_to_host(uint8_t *frame, size_t len);
