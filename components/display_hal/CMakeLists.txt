idf_component_register(
    SRCS         "display_hal.c" "font5x7.c"
    INCLUDE_DIRS "include"
    REQUIRES     channel_router log esp_driver_gpio esp_driver_spi esp_psram
)
