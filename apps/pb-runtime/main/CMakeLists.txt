idf_component_register(
    SRCS
        "main.c"
        "pb_cloud.c"
    INCLUDE_DIRS "."
    REQUIRES
        pb-hal
        pb-app-protocol
        pb-runtime
        esp_event
        esp_http_client
        esp_timer
        esp_netif
        esp_wifi
        mbedtls
        nvs_flash
)
