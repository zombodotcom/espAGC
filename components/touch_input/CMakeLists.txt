idf_component_register(
    SRCS         "touch_input.c" "cst820.c"
    INCLUDE_DIRS "include"
    REQUIRES     channel_router log esp_driver_gpio esp_driver_i2c esp_timer freertos
)
