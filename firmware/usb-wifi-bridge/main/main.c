/*
 * USB Wi-Fi bridge for the DJI camera network.
 *
 * The ESP32-S3 joins the camera's 2.4 GHz access point and shows up on the laptop as a USB NCM
 * network adapter plus a CDC-ACM command console. The laptop keeps its own Wi-Fi for the internet
 * and reaches the camera (192.168.2.1) through this adapter.
 */

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"

#include "bridge.h"
#include "console.h"
#include "ota_update.h"
#include "wifi_link.h"

static const char *TAG = "main";

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase");
        err = nvs_flash_init();
    }
    return err;
}

void app_main(void)
{
    ESP_ERROR_CHECK(init_nvs());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_ERROR_CHECK(console_start());
    ESP_ERROR_CHECK(bridge_init());
    ESP_ERROR_CHECK(wifi_link_start());
    ota_start_rollback_guard();

    ESP_LOGI(TAG, "USB Wi-Fi bridge ready (type 'help' on the serial console)");
}
