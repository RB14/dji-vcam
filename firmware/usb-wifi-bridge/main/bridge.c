#include "bridge.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_private/wifi.h"
#include "esp_wifi.h"
#include "device/usbd_pvt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "tinyusb_net.h"
#include "tusb.h"

#include "net_filter.h"

static const char *TAG = "bridge";

static volatile bool s_wifi_up;
static volatile bool s_filter_enabled = true;
static bridge_stats_t s_stats;

static esp_err_t usb_to_wifi(void *buffer, uint16_t len, void *ctx)
{
    (void)ctx;
    if (!s_wifi_up) {
        s_stats.to_wifi_dropped++;
        return ESP_OK;
    }
    if (esp_wifi_internal_tx(WIFI_IF_STA, buffer, len) == ESP_OK) {
        s_stats.to_wifi_frames++;
        s_stats.to_wifi_bytes += len;
    } else {
        s_stats.to_wifi_dropped++;
    }
    return ESP_OK;
}

static void free_wifi_rx_buffer(void *eb, void *ctx)
{
    (void)ctx;
    esp_wifi_internal_free_rx_buffer(eb);
}

/*
 * Wi-Fi -> USB path.
 *
 * The Wi-Fi RX callback only queues the frame. The usb_tx task wakes up when frames are queued and
 * asks the TinyUSB task (via usbd_defer_func) to move as many as fit into NCM transfer blocks (NTBs);
 * NTBs batch several frames per USB transfer and the queue absorbs keyframe bursts. Exactly one pump
 * request is in flight at a time, and a request that never reports back (a deferred call dropped by a
 * full TinyUSB event queue) is simply re-issued, so the path cannot deadlock.
 */

/* Mirrors esp_tinyusb's private packet_t: its tud_network_xmit_cb() copies `buffer` into the NTB
 * during tud_network_xmit() and passes `buff_free_arg` to our free_wifi_rx_buffer(). */
typedef struct {
    void *buffer;
    void *buff_free_arg;
    uint16_t len;
    esp_err_t result;
} usb_tx_packet_t;

#define TX_QUEUE_DEPTH          48
#define PUMP_REPLY_TIMEOUT_MS   5
#define PUMP_TASK_PRIORITY      10
#define PUMP_DONE               1
#define PUMP_BLOCKED            2   /* every NTB is in flight */

static QueueHandle_t s_tx_queue;
static TaskHandle_t s_pump_task;

/* Runs in the TinyUSB task. */
static void pump_to_usb(void *arg)
{
    (void)arg;
    uint32_t result = PUMP_DONE;
    usb_tx_packet_t packet;
    while (xQueuePeek(s_tx_queue, &packet, 0) == pdTRUE) {
        if (!tud_mounted()) {
            xQueueReceive(s_tx_queue, &packet, 0);
            esp_wifi_internal_free_rx_buffer(packet.buff_free_arg);
            s_stats.to_host_dropped++;
            continue;
        }
        if (!tud_network_can_xmit(packet.len)) {
            result = PUMP_BLOCKED;
            s_stats.pump_blocked++;
            break;
        }
        xQueueReceive(s_tx_queue, &packet, 0);
        tud_network_xmit(&packet, packet.len);
        s_stats.to_host_frames++;
        s_stats.to_host_bytes += packet.len;
    }
    s_stats.pump_runs++;
    xTaskNotify(s_pump_task, result, eSetValueWithOverwrite);
}

static void pump_task(void *arg)
{
    (void)arg;
    usb_tx_packet_t packet;
    for (;;) {
        xQueuePeek(s_tx_queue, &packet, portMAX_DELAY);
        xTaskNotifyStateClear(NULL);
        usbd_defer_func(pump_to_usb, NULL, false);
        uint32_t result = 0;
        if (xTaskNotifyWait(0, UINT32_MAX, &result, pdMS_TO_TICKS(PUMP_REPLY_TIMEOUT_MS)) != pdTRUE) {
            s_stats.pump_lost++;
            continue;
        }
        if (result == PUMP_BLOCKED) {
            vTaskDelay(1);
        }
    }
}

static esp_err_t wifi_to_usb(void *buffer, uint16_t len, void *eb)
{
    if (s_filter_enabled) {
        switch (net_filter_to_host(buffer, len)) {
        case NET_FILTER_DROP:
            s_stats.to_host_dropped++;
            esp_wifi_internal_free_rx_buffer(eb);
            return ESP_OK;
        case NET_FILTER_MODIFIED:
            s_stats.to_host_rewritten++;
            break;
        case NET_FILTER_PASS:
            break;
        }
    }
    usb_tx_packet_t packet = {.buffer = buffer, .buff_free_arg = eb, .len = len};
    if (!tud_mounted() || xQueueSend(s_tx_queue, &packet, 0) != pdTRUE) {
        s_stats.to_host_dropped++;
        esp_wifi_internal_free_rx_buffer(eb);
    }
    return ESP_OK;
}

esp_err_t bridge_init(void)
{
    s_tx_queue = xQueueCreate(TX_QUEUE_DEPTH, sizeof(usb_tx_packet_t));
    ESP_RETURN_ON_FALSE(s_tx_queue, ESP_ERR_NO_MEM, TAG, "tx queue");
    ESP_RETURN_ON_FALSE(xTaskCreate(pump_task, "usb_tx", 3072, NULL, PUMP_TASK_PRIORITY, &s_pump_task) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "usb_tx task");

    tinyusb_net_config_t net_config = {
        .on_recv_callback = usb_to_wifi,
        .free_tx_buffer = free_wifi_rx_buffer,
        .user_context = NULL,
    };
    ESP_RETURN_ON_ERROR(esp_read_mac(net_config.mac_addr, ESP_MAC_WIFI_STA), TAG, "read MAC");
    const uint8_t *m = net_config.mac_addr;
    ESP_LOGI(TAG, "USB NCM MAC (= Wi-Fi STA MAC) %02x:%02x:%02x:%02x:%02x:%02x",
             m[0], m[1], m[2], m[3], m[4], m[5]);
    return tinyusb_net_init(&net_config);
}

/* TinyUSB hook: report "cable unplugged" until the Wi-Fi station is associated. */
bool tud_network_default_link_state_cb(void)
{
    return false;
}

void bridge_set_wifi_link(bool up)
{
    s_wifi_up = up;
    esp_wifi_internal_reg_rxcb(WIFI_IF_STA, up ? wifi_to_usb : NULL);
    /* Mirrors the association as the adapter's cable state, so the host re-runs DHCP on join. */
    tud_network_link_state(0, up);
}

void bridge_set_filter_enabled(bool enabled)
{
    s_filter_enabled = enabled;
}

bool bridge_filter_enabled(void)
{
    return s_filter_enabled;
}

void bridge_get_stats(bridge_stats_t *out)
{
    *out = s_stats;
    out->tx_queued = uxQueueMessagesWaiting(s_tx_queue);
}
