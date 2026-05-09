idf_component_register(
    SRCS         "rgb_gpio.c"
    INCLUDE_DIRS "include"
    REQUIRES     log esp_driver_gpio
)
