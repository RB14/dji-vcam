/*
 * Firmware updates received over the USB console, with automatic rollback.
 *
 * A new image is written to the next OTA slot and verified against the SHA-256 the host announced.
 * After the reboot it runs "pending verification": it must be confirmed (ota_confirm_running_app,
 * called once the console handles a command) within ROLLBACK_GUARD_S, otherwise the firmware restarts
 * and the bootloader falls back to the previous image. A broken update therefore cannot take away the
 * ability to update again.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define OTA_SHA256_LEN 32

esp_err_t ota_update_begin(size_t image_size);
esp_err_t ota_update_write(const void *data, size_t len);
/* Verifies the hash and selects the new image for the next boot. */
esp_err_t ota_update_finish(const uint8_t expected_sha256[OTA_SHA256_LEN]);
void ota_update_abort(void);

/* Marks the running image valid if it is still pending verification (idempotent). */
void ota_confirm_running_app(void);

/* If the running image is pending verification, restarts (-> rollback) unless confirmed in time. */
void ota_start_rollback_guard(void);

/* One-line description: version, build time, running partition and its OTA state. */
void ota_describe_running_app(char *out, size_t out_len);
