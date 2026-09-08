idf_component_register(
    SRCS "main.c" "ct1668.c" "ct1668_key_test.c" "sdc251_keys.c" "sdc251_panel.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_uart esp_rom freertos log
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
