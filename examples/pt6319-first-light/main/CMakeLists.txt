idf_component_register(
    SRCS "main.c" "pt6319.c" "vfd_mapper.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_gpio esp_driver_uart esp_timer esp_rom esp_common log freertos
)
