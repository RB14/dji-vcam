idf_component_register(SRCS "main.c" "bridge.c" "console.c" "net_filter.c" "wifi_link.c" "ota_update.c"
                       INCLUDE_DIRS ""
                       PRIV_REQUIRES esp_wifi esp_timer nvs_flash app_update mbedtls
                       )
