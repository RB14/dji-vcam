#include "wifi_link.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include "bridge.h"

#define NVS_NAMESPACE     "wifi_link"
#define NVS_KEY_SSID      "ssid"
#define NVS_KEY_PASSWORD  "pass"
#define MAX_SCAN_RESULTS  24

static const char *TAG = "wifi";

static wifi_config_t s_config;
static volatile bool s_configured;
static volatile bool s_connected;
static volatile bool s_scanning;
static uint32_t s_reconnects;
static uint32_t s_drops;
static int64_t s_connected_since_us;
static wifi_link_drop_t s_history[WIFI_LINK_HISTORY];  /* ring buffer */
static uint8_t s_history_next;
static uint8_t s_history_len;

static uint32_t uptime_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

static void record_drop(uint16_t reason)
{
    s_history[s_history_next] = (wifi_link_drop_t){.reason = reason, .uptime_s = uptime_s()};
    s_history_next = (s_history_next + 1) % WIFI_LINK_HISTORY;
    if (s_history_len < WIFI_LINK_HISTORY) {
        s_history_len++;
    }
    s_drops++;
}

const char *wifi_link_reason_name(uint16_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE: return "AUTH_EXPIRE";
    case WIFI_REASON_AUTH_LEAVE: return "AUTH_LEAVE";
    case WIFI_REASON_ASSOC_EXPIRE: return "ASSOC_EXPIRE";
    case WIFI_REASON_ASSOC_LEAVE: return "ASSOC_LEAVE";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4WAY_HANDSHAKE_TIMEOUT";
    case WIFI_REASON_BEACON_TIMEOUT: return "BEACON_TIMEOUT";
    case WIFI_REASON_NO_AP_FOUND: return "NO_AP_FOUND";
    case WIFI_REASON_AUTH_FAIL: return "AUTH_FAIL";
    case WIFI_REASON_ASSOC_FAIL: return "ASSOC_FAIL";
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "HANDSHAKE_TIMEOUT";
    case WIFI_REASON_CONNECTION_FAIL: return "CONNECTION_FAIL";
    default: return "other";
    }
}

static esp_err_t load_credentials(void)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs), TAG, "no stored credentials");
    size_t ssid_len = sizeof(s_config.sta.ssid);
    size_t pass_len = sizeof(s_config.sta.password);
    esp_err_t err = nvs_get_str(nvs, NVS_KEY_SSID, (char *)s_config.sta.ssid, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(nvs, NVS_KEY_PASSWORD, (char *)s_config.sta.password, &pass_len);
    }
    nvs_close(nvs);
    return err;
}

static esp_err_t save_credentials(const char *ssid, const char *password)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG, "nvs_open");
    esp_err_t err = nvs_set_str(nvs, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, NVS_KEY_PASSWORD, password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == WIFI_EVENT_STA_CONNECTED) {
        const wifi_event_sta_connected_t *ev = data;
        ESP_LOGI(TAG, "connected to '%.*s' on channel %u", ev->ssid_len, ev->ssid, ev->channel);
        s_connected = true;
        s_connected_since_us = esp_timer_get_time();
        bridge_set_wifi_link(true);
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *ev = data;
        bool was_connected = s_connected;
        s_connected = false;
        bridge_set_wifi_link(false);
        if (was_connected) {
            record_drop(ev->reason);
            ESP_LOGW(TAG, "link dropped (reason %u %s), retrying", ev->reason, wifi_link_reason_name(ev->reason));
        } else if (s_reconnects % 20 == 0) {
            ESP_LOGW(TAG, "still not connected (reason %u %s), retrying", ev->reason, wifi_link_reason_name(ev->reason));
        }
        if (s_configured && !s_scanning) {
            s_reconnects++;
            esp_wifi_connect();
        }
    }
}

static esp_err_t apply_and_connect(void)
{
    s_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    s_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &s_config), TAG, "set_config");
    s_configured = true;
    ESP_LOGI(TAG, "connecting to '%s'", (const char *)s_config.sta.ssid);
    return esp_wifi_connect();
}

esp_err_t wifi_link_start(void)
{
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "esp_wifi_init");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL),
                        TAG, "register handler");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "set_storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set_mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start");
    /* Modem sleep delays downlink frames until the next beacon: fatal for live video latency. */
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "set_ps");

    if (load_credentials() != ESP_OK) {
        ESP_LOGW(TAG, "no credentials stored; use: wifi <ssid> <password>");
        return ESP_OK;
    }
    return apply_and_connect();
}

esp_err_t wifi_link_set_credentials(const char *ssid, const char *password)
{
    if (strlen(ssid) >= sizeof(s_config.sta.ssid) || strlen(password) >= sizeof(s_config.sta.password)) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(save_credentials(ssid, password), TAG, "save credentials");
    s_configured = false;
    esp_wifi_disconnect();
    memset(&s_config, 0, sizeof(s_config));
    strlcpy((char *)s_config.sta.ssid, ssid, sizeof(s_config.sta.ssid));
    strlcpy((char *)s_config.sta.password, password, sizeof(s_config.sta.password));
    return apply_and_connect();
}

esp_err_t wifi_link_forget(void)
{
    s_configured = false;
    esp_wifi_disconnect();
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG, "nvs_open");
    nvs_erase_all(nvs);
    esp_err_t err = nvs_commit(nvs);
    nvs_close(nvs);
    memset(&s_config, 0, sizeof(s_config));
    return err;
}

static const char *auth_name(wifi_auth_mode_t mode)
{
    switch (mode) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "wep";
    case WIFI_AUTH_WPA_PSK: return "wpa";
    case WIFI_AUTH_WPA2_PSK: return "wpa2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "wpa/wpa2";
    case WIFI_AUTH_WPA3_PSK: return "wpa3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "wpa2/wpa3";
    default: return "other";
    }
}

esp_err_t wifi_link_scan(void)
{
    s_scanning = true;
    if (s_configured && !s_connected) {
        /* A pending connection attempt blocks scanning. */
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    wifi_scan_config_t scan_cfg = {.show_hidden = true};
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err == ESP_OK) {
        static wifi_ap_record_t records[MAX_SCAN_RESULTS];
        uint16_t count = MAX_SCAN_RESULTS;
        esp_wifi_scan_get_ap_records(&count, records);
        ESP_LOGI(TAG, "scan: %u networks", count);
        for (uint16_t i = 0; i < count; i++) {
            const wifi_ap_record_t *r = &records[i];
            ESP_LOGI(TAG, "  %4d dBm  ch %2u  %-9s  %02x:%02x:%02x:%02x:%02x:%02x  '%s'",
                     r->rssi, r->primary, auth_name(r->authmode),
                     r->bssid[0], r->bssid[1], r->bssid[2], r->bssid[3], r->bssid[4], r->bssid[5],
                     (const char *)r->ssid);
        }
    } else {
        ESP_LOGE(TAG, "scan failed: %s", esp_err_to_name(err));
    }
    s_scanning = false;
    if (s_configured && !s_connected) {
        esp_wifi_connect();
    }
    return err;
}

void wifi_link_get_status(wifi_link_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->configured = s_configured;
    out->connected = s_connected;
    out->reconnects = s_reconnects;
    out->drops = s_drops;
    out->connected_s = s_connected ? (uint32_t)((esp_timer_get_time() - s_connected_since_us) / 1000000) : 0;
    out->history_len = s_history_len;
    for (uint8_t i = 0; i < s_history_len; i++) {
        out->history[i] = s_history[(s_history_next + WIFI_LINK_HISTORY - 1 - i) % WIFI_LINK_HISTORY];
    }
    strlcpy(out->ssid, (const char *)s_config.sta.ssid, sizeof(out->ssid));
    wifi_ap_record_t ap;
    if (s_connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        out->rssi = ap.rssi;
        out->channel = ap.primary;
    }
}
