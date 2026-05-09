idf_component_register(
    SRCS         "apa102_status.c"
    INCLUDE_DIRS "include"
    REQUIRES     channel_router board_tdongle_c5 esp_driver_gpio
)
