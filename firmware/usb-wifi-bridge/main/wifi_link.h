/*
 * Wi-Fi station management: credentials persisted in NVS, automatic reconnection, scanning.
 *
 * The station runs without an IP stack of its own; it only carries bridged frames.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define WIFI_LINK_HISTORY 8

typedef struct {
    uint16_t reason;       /* wifi_err_reason_t */
    uint32_t uptime_s;     /* when the established link dropped */
} wifi_link_drop_t;

typedef struct {
    bool configured;
    bool connected;
    char ssid[33];
    int8_t rssi;
    uint8_t channel;
    uint32_t reconnects;       /* connection attempts after a failure or drop */
    uint32_t drops;            /* established links that went down */
    uint32_t connected_s;      /* how long the current link has been up */
    uint8_t history_len;       /* most recent drops first */
    wifi_link_drop_t history[WIFI_LINK_HISTORY];
} wifi_link_status_t;

/* Short name for a Wi-Fi disconnect reason code. */
const char *wifi_link_reason_name(uint16_t reason);

/* Starts Wi-Fi and connects if credentials are stored. */
esp_err_t wifi_link_start(void);

/* Stores new credentials and (re)connects with them. */
esp_err_t wifi_link_set_credentials(const char *ssid, const char *password);

/* Erases stored credentials and disconnects. */
esp_err_t wifi_link_forget(void);

/* Leaves the camera's network (a clean deauthentication) and joins it again at once (diagnostics). */
esp_err_t wifi_link_rejoin(void);

/* Blocking scan; prints results through the log. */
esp_err_t wifi_link_scan(void);

void wifi_link_get_status(wifi_link_status_t *out);
