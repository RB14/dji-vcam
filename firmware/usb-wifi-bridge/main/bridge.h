/*
 * Layer-2 bridge between the USB NCM interface (host side) and the Wi-Fi station (camera side).
 *
 * The USB interface reuses the Wi-Fi station MAC address, so frames from the host can be
 * transmitted over Wi-Fi as-is and the camera's network sees the laptop as a regular client.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint32_t to_host_frames;
    uint64_t to_host_bytes;
    uint32_t to_host_dropped;
    uint32_t to_host_rewritten;
    uint32_t to_wifi_frames;
    uint64_t to_wifi_bytes;
    uint32_t to_wifi_dropped;
    uint32_t tx_queued;      /* frames waiting for USB right now */
    uint32_t pump_runs;      /* USB pump invocations */
    uint32_t pump_blocked;   /* pumps that found every NCM transfer block in flight */
    uint32_t pump_lost;      /* pump requests that never reported back (re-issued) */
} bridge_stats_t;

/* Installs the NCM class. TinyUSB itself must already be installed. */
esp_err_t bridge_init(void);

/* Called by the Wi-Fi layer when the station link comes up or goes down. */
void bridge_set_wifi_link(bool up);

void bridge_set_filter_enabled(bool enabled);
bool bridge_filter_enabled(void);
void bridge_get_stats(bridge_stats_t *out);
