idf_component_register(
    SRCS "main.c" "pt6312_tools.c" "vfd_mapper.c" "vfd_demo.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES pt6312 vfd esp_driver_uart esp_timer esp_common log freertos
)
