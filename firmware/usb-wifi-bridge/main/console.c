#include "console.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"

#include "bridge.h"
#include "ota_update.h"
#include "wifi_link.h"

#define CDC_PORT          TINYUSB_CDC_ACM_0
#define CONSOLE_LINE_MAX          160
#define MAX_ARGS          4
#define LINE_QUEUE_DEPTH  4

static const char *TAG = "console";

#define FLUSH_TIMEOUT_MS  20
#define OTA_CHUNK         4096
#define OTA_IDLE_TIMEOUT_MS 10000

static QueueHandle_t s_lines;
static SemaphoreHandle_t s_write_lock;
static volatile bool s_host_listening;
static volatile bool s_binary_mode;  /* an OTA image is streaming: leave RX bytes to the console task */
static char s_rx_line[CONSOLE_LINE_MAX];
static size_t s_rx_len;

/* ---- output ---------------------------------------------------------------------------- */

/* Queues one character, draining the TX FIFO to the host when it is full. */
static bool cdc_put(char c)
{
    if (tinyusb_cdcacm_write_queue_char(CDC_PORT, c) == 1) {
        return true;
    }
    tinyusb_cdcacm_write_flush(CDC_PORT, pdMS_TO_TICKS(FLUSH_TIMEOUT_MS));
    return tinyusb_cdcacm_write_queue_char(CDC_PORT, c) == 1;
}

static void cdc_write(const char *text, size_t len)
{
    /* Nobody has the port open: drop output rather than stall the caller. */
    if (!s_host_listening || xSemaphoreTake(s_write_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    for (size_t i = 0; i < len; i++) {
        if ((text[i] == '\n' && !cdc_put('\r')) || !cdc_put(text[i])) {
            break;
        }
    }
    tinyusb_cdcacm_write_flush(CDC_PORT, 0);
    xSemaphoreGive(s_write_lock);
}

static int log_to_cdc(const char *fmt, va_list args)
{
    char buf[256];
    va_list copy;
    va_copy(copy, args);
    int len = vsnprintf(buf, sizeof(buf), fmt, copy);
    va_end(copy);
    if (len > 0) {
        cdc_write(buf, len < (int)sizeof(buf) ? (size_t)len : sizeof(buf) - 1);
    }
    return vprintf(fmt, args); /* keep UART0 output as well */
}

static void con_printf(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len > 0) {
        cdc_write(buf, len < (int)sizeof(buf) ? (size_t)len : sizeof(buf) - 1);
    }
}

/* ---- input ----------------------------------------------------------------------------- */

static void queue_line(const char *line)
{
    char copy[CONSOLE_LINE_MAX];
    strlcpy(copy, line, sizeof(copy));
    xQueueSend(s_lines, copy, 0);
}

static void on_cdc_rx(int itf, cdcacm_event_t *event)
{
    (void)event;
    if (s_binary_mode) {
        return;
    }
    uint8_t buf[64];
    size_t n = 0;
    if (tinyusb_cdcacm_read(itf, buf, sizeof(buf), &n) != ESP_OK) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        char c = (char)buf[i];
        if (c == '\r' || c == '\n') {
            if (s_rx_len > 0) {
                s_rx_line[s_rx_len] = '\0';
                queue_line(s_rx_line);
                s_rx_len = 0;
            }
        } else if (s_rx_len < CONSOLE_LINE_MAX - 1) {
            s_rx_line[s_rx_len++] = c;
        }
    }
}

static void on_line_state(int itf, cdcacm_event_t *event)
{
    (void)itf;
    s_host_listening = event->line_state_changed_data.dtr;
}

/* ---- commands -------------------------------------------------------------------------- */

/* Splits a line into whitespace separated words; double quotes group words. */
static int tokenize(char *line, char *argv[], int max_args)
{
    int argc = 0;
    char *p = line;
    while (*p && argc < max_args) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (!*p) {
            break;
        }
        char end = ' ';
        if (*p == '"') {
            end = '"';
            p++;
        }
        argv[argc++] = p;
        while (*p && *p != end && !(end == ' ' && *p == '\t')) {
            p++;
        }
        if (*p) {
            *p++ = '\0';
        }
    }
    return argc;
}

static void cmd_help(void)
{
    con_printf("commands:\n"
               "  status                  link, filter and traffic counters\n"
               "  scan                    list nearby 2.4 GHz networks\n"
               "  wifi <ssid> <password>  store credentials and connect (quote SSIDs with spaces)\n"
               "  forget                  erase credentials and disconnect\n"
               "  filter on|off           strip gateway/DNS from DHCP replies (default on)\n"
               "  version                 firmware version and running partition\n"
               "  ota <size> <sha256>     receive a firmware image (used by tools/bridge_ota.py)\n"
               "  reboot                  restart the firmware\n");
}

static bool parse_sha256(const char *hex, uint8_t out[OTA_SHA256_LEN])
{
    if (strlen(hex) != OTA_SHA256_LEN * 2) {
        return false;
    }
    for (int i = 0; i < OTA_SHA256_LEN; i++) {
        char byte[3] = {hex[2 * i], hex[2 * i + 1], '\0'};
        char *end = NULL;
        out[i] = (uint8_t)strtoul(byte, &end, 16);
        if (*end != '\0') {
            return false;
        }
    }
    return true;
}

/* Reads exactly `size` bytes of image from the CDC port straight into the next OTA slot. */
static esp_err_t receive_image(size_t size)
{
    static uint8_t chunk[OTA_CHUNK];
    size_t received = 0;
    TickType_t last_data = xTaskGetTickCount();
    while (received < size) {
        size_t want = size - received < sizeof(chunk) ? size - received : sizeof(chunk);
        size_t got = 0;
        tinyusb_cdcacm_read(CDC_PORT, chunk, want, &got);
        if (got == 0) {
            if (xTaskGetTickCount() - last_data > pdMS_TO_TICKS(OTA_IDLE_TIMEOUT_MS)) {
                return ESP_ERR_TIMEOUT;
            }
            vTaskDelay(1);
            continue;
        }
        last_data = xTaskGetTickCount();
        ESP_RETURN_ON_ERROR(ota_update_write(chunk, got), TAG, "ota write");
        received += got;
    }
    return ESP_OK;
}

static void cmd_ota(const char *size_arg, const char *sha_arg)
{
    uint8_t sha[OTA_SHA256_LEN];
    size_t size = strtoul(size_arg, NULL, 10);
    if (size == 0 || !parse_sha256(sha_arg, sha)) {
        con_printf("ota: error bad arguments\n");
        return;
    }
    esp_err_t err = ota_update_begin(size);
    if (err != ESP_OK) {
        con_printf("ota: error %s\n", esp_err_to_name(err));
        return;
    }
    s_binary_mode = true;
    con_printf("ota: ready\n");
    err = receive_image(size);
    s_binary_mode = false;
    if (err == ESP_OK) {
        err = ota_update_finish(sha);
    } else {
        ota_update_abort();
    }
    if (err != ESP_OK) {
        con_printf("ota: error %s\n", esp_err_to_name(err));
        return;
    }
    con_printf("ota: ok, rebooting\n");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

static void cmd_status(void)
{
    wifi_link_status_t link;
    bridge_stats_t stats;
    wifi_link_get_status(&link);
    bridge_get_stats(&stats);
    con_printf("wifi: %s ssid='%s' rssi=%d ch=%u up=%lus drops=%lu attempts=%lu\n",
               link.connected ? "connected" : (link.configured ? "connecting" : "unconfigured"),
               link.ssid, link.rssi, link.channel, (unsigned long)link.connected_s,
               (unsigned long)link.drops, (unsigned long)link.reconnects);
    for (uint8_t i = 0; i < link.history_len; i++) {
        con_printf("  drop at %lus: reason %u %s\n", (unsigned long)link.history[i].uptime_s,
                   link.history[i].reason, wifi_link_reason_name(link.history[i].reason));
    }
    con_printf("filter: %s\n", bridge_filter_enabled() ? "on" : "off");
    con_printf("to host: %lu frames %llu bytes, %lu dropped, %lu rewritten\n",
               (unsigned long)stats.to_host_frames, (unsigned long long)stats.to_host_bytes,
               (unsigned long)stats.to_host_dropped, (unsigned long)stats.to_host_rewritten);
    con_printf("to wifi: %lu frames %llu bytes, %lu dropped\n",
               (unsigned long)stats.to_wifi_frames, (unsigned long long)stats.to_wifi_bytes,
               (unsigned long)stats.to_wifi_dropped);
    con_printf("usb pump: %lu runs, %lu blocked, %lu lost, %lu queued (peak %lu)\n",
               (unsigned long)stats.pump_runs, (unsigned long)stats.pump_blocked,
               (unsigned long)stats.pump_lost, (unsigned long)stats.tx_queued, (unsigned long)stats.tx_queue_peak);
    con_printf("heap: %lu free (internal %lu, PSRAM %lu)\n", (unsigned long)esp_get_free_heap_size(),
               (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
               (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void run_command(char *line)
{
    char *argv[MAX_ARGS];
    int argc = tokenize(line, argv, MAX_ARGS);
    if (argc == 0) {
        return;
    }
    const char *cmd = argv[0];
    if (strcmp(cmd, "help") == 0) {
        cmd_help();
    } else if (strcmp(cmd, "status") == 0) {
        cmd_status();
    } else if (strcmp(cmd, "scan") == 0) {
        wifi_link_scan();
    } else if (strcmp(cmd, "wifi") == 0 && argc == 3) {
        esp_err_t err = wifi_link_set_credentials(argv[1], argv[2]);
        con_printf("wifi: %s\n", esp_err_to_name(err));
    } else if (strcmp(cmd, "forget") == 0) {
        con_printf("forget: %s\n", esp_err_to_name(wifi_link_forget()));
    } else if (strcmp(cmd, "filter") == 0 && argc == 2) {
        bridge_set_filter_enabled(strcmp(argv[1], "off") != 0);
        con_printf("filter: %s\n", bridge_filter_enabled() ? "on" : "off");
    } else if (strcmp(cmd, "version") == 0) {
        char desc[160];
        ota_describe_running_app(desc, sizeof(desc));
        con_printf("version: %s\n", desc);
    } else if (strcmp(cmd, "ota") == 0 && argc == 3) {
        cmd_ota(argv[1], argv[2]);
    } else if (strcmp(cmd, "reboot") == 0) {
        esp_restart();
    } else {
        con_printf("unknown command '%s' (try: help)\n", cmd);
    }
}

static void console_task(void *arg)
{
    (void)arg;
    char line[CONSOLE_LINE_MAX];
    for (;;) {
        if (xQueueReceive(s_lines, line, portMAX_DELAY) == pdTRUE) {
            /* Echo the command, but never the Wi-Fi password. */
            con_printf("> %s\n", strncmp(line, "wifi ", 5) == 0 ? "wifi <ssid> <password>" : line);
            /* A console that handles commands is what remote updates depend on: a new image that
             * gets this far has proven itself. */
            ota_confirm_running_app();
            run_command(line);
        }
    }
}

esp_err_t console_start(void)
{
    s_lines = xQueueCreate(LINE_QUEUE_DEPTH, CONSOLE_LINE_MAX);
    s_write_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lines && s_write_lock, ESP_ERR_NO_MEM, TAG, "alloc");

    const tinyusb_config_cdcacm_t acm_cfg = {
        .cdc_port = CDC_PORT,
        .callback_rx = on_cdc_rx,
        .callback_line_state_changed = on_line_state,
    };
    ESP_RETURN_ON_ERROR(tinyusb_cdcacm_init(&acm_cfg), TAG, "cdcacm init");
    esp_log_set_vprintf(log_to_cdc);

    BaseType_t ok = xTaskCreate(console_task, "console", 4096, NULL, 5, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
