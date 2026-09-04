idf_component_register(
    SRCS
        "main.c"
        "pb_cloud.c"
        "pb_view.c"
        "pb_actions.c"
    INCLUDE_DIRS "."
    REQUIRES
        old-panel
        pb-app-protocol
        esp_event
        esp_http_client
        esp_netif
        esp_wifi
        mbedtls
        nvs_flash
)
