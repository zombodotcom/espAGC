idf_component_register(
    SRCS         "board_init.c"
    INCLUDE_DIRS "include"
    REQUIRES     log esp_driver_gpio display_hal touch_input led_status
)
