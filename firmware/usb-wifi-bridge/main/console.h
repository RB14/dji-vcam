/*
 * Command console on the USB CDC-ACM port that sits next to the NCM network interface.
 *
 * All ESP_LOG output is mirrored to this port. Commands are line based; type "help".
 */
#pragma once

#include "esp_err.h"

/* Installs CDC-ACM port 0 and starts the console task. TinyUSB must already be installed. */
esp_err_t console_start(void);
