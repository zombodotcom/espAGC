idf_component_register(
    SRCS         "touch_input.c" "xpt2046.c"
    INCLUDE_DIRS "include"
    REQUIRES     channel_router log esp_driver_gpio esp_driver_spi esp_timer freertos
)
