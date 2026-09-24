/*
 * Wi-Fi station management: credentials persisted in NVS, automatic reconnection, scanning.
 *
 * The station runs without an IP stack of its own; it only carries bridged frames.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool configured;
    bool connected;
    char ssid[33];
    int8_t rssi;
    uint8_t channel;
    uint32_t reconnects;
} wifi_link_status_t;

/* Starts Wi-Fi and connects if credentials are stored. */
esp_err_t wifi_link_start(void);

/* Stores new credentials and (re)connects with them. */
esp_err_t wifi_link_set_credentials(const char *ssid, const char *password);

/* Erases stored credentials and disconnects. */
esp_err_t wifi_link_forget(void);

/* Blocking scan; prints results through the log. */
esp_err_t wifi_link_scan(void);

void wifi_link_get_status(wifi_link_status_t *out);
