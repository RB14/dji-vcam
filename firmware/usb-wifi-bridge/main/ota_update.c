#include "ota_update.h"

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"

#define ROLLBACK_GUARD_S 90

static const char *TAG = "ota";

static const esp_partition_t *s_target;
static esp_ota_handle_t s_handle;
static mbedtls_sha256_context s_sha;
static size_t s_expected;
static size_t s_written;
static volatile bool s_confirmed;

esp_err_t ota_update_begin(size_t image_size)
{
    ota_update_abort();
    s_target = esp_ota_get_next_update_partition(NULL);
    ESP_RETURN_ON_FALSE(s_target, ESP_ERR_NOT_FOUND, TAG, "no OTA partition");
    ESP_RETURN_ON_FALSE(image_size <= s_target->size, ESP_ERR_INVALID_SIZE, TAG,
                        "image %u > slot %lu", (unsigned)image_size, (unsigned long)s_target->size);
    ESP_RETURN_ON_ERROR(esp_ota_begin(s_target, OTA_WITH_SEQUENTIAL_WRITES, &s_handle), TAG, "begin");
    mbedtls_sha256_init(&s_sha);
    mbedtls_sha256_starts(&s_sha, 0);
    s_expected = image_size;
    s_written = 0;
    ESP_LOGI(TAG, "receiving %u bytes into '%s'", (unsigned)image_size, s_target->label);
    return ESP_OK;
}

esp_err_t ota_update_write(const void *data, size_t len)
{
    ESP_RETURN_ON_FALSE(s_handle, ESP_ERR_INVALID_STATE, TAG, "no update in progress");
    ESP_RETURN_ON_FALSE(s_written + len <= s_expected, ESP_ERR_INVALID_SIZE, TAG, "too much data");
    ESP_RETURN_ON_ERROR(esp_ota_write(s_handle, data, len), TAG, "write");
    mbedtls_sha256_update(&s_sha, data, len);
    s_written += len;
    return ESP_OK;
}

esp_err_t ota_update_finish(const uint8_t expected_sha256[OTA_SHA256_LEN])
{
    ESP_RETURN_ON_FALSE(s_handle, ESP_ERR_INVALID_STATE, TAG, "no update in progress");
    uint8_t actual[OTA_SHA256_LEN];
    mbedtls_sha256_finish(&s_sha, actual);
    mbedtls_sha256_free(&s_sha);
    if (s_written != s_expected || memcmp(actual, expected_sha256, OTA_SHA256_LEN) != 0) {
        ESP_LOGE(TAG, "image rejected: %u/%u bytes, hash %s", (unsigned)s_written, (unsigned)s_expected,
                 s_written == s_expected ? "mismatch" : "n/a");
        esp_ota_abort(s_handle);
        s_handle = 0;
        return ESP_ERR_INVALID_CRC;
    }
    esp_err_t err = esp_ota_end(s_handle);  /* also validates the image structure */
    s_handle = 0;
    ESP_RETURN_ON_ERROR(err, TAG, "end");
    ESP_RETURN_ON_ERROR(esp_ota_set_boot_partition(s_target), TAG, "set boot partition");
    ESP_LOGI(TAG, "image verified, '%s' boots next", s_target->label);
    return ESP_OK;
}

void ota_update_abort(void)
{
    if (s_handle) {
        esp_ota_abort(s_handle);
        mbedtls_sha256_free(&s_sha);
        s_handle = 0;
    }
}

static bool running_app_pending_verify(void)
{
    esp_ota_img_states_t state;
    return esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
           state == ESP_OTA_IMG_PENDING_VERIFY;
}

void ota_confirm_running_app(void)
{
    if (s_confirmed) {
        return;
    }
    s_confirmed = true;
    if (running_app_pending_verify() && esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        ESP_LOGI(TAG, "new firmware confirmed");
    }
}

static void rollback_guard_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(ROLLBACK_GUARD_S * 1000));
    if (!s_confirmed && running_app_pending_verify()) {
        ESP_LOGE(TAG, "new firmware never confirmed; restarting to roll back");
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    }
    vTaskDelete(NULL);
}

void ota_start_rollback_guard(void)
{
    if (running_app_pending_verify()) {
        ESP_LOGW(TAG, "running unconfirmed firmware; confirm within %d s or it rolls back", ROLLBACK_GUARD_S);
        xTaskCreate(rollback_guard_task, "ota_guard", 2560, NULL, 2, NULL);
    }
}

void ota_describe_running_app(char *out, size_t out_len)
{
    const esp_app_desc_t *app = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    esp_ota_get_state_partition(running, &state);
    const char *state_name = state == ESP_OTA_IMG_VALID ? "valid"
                             : state == ESP_OTA_IMG_PENDING_VERIFY ? "pending-verify"
                             : state == ESP_OTA_IMG_NEW ? "new" : "factory/undefined";
    snprintf(out, out_len, "%s %s built %s %s, running from '%s' (%s)", app->project_name, app->version,
             app->date, app->time, running->label, state_name);
}
