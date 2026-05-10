# components/display_hal/CMakeLists.txt
idf_component_register(
    SRCS         "display_hal.c" "font5x7.c" "panel_st7789.c" "dsky_layout.c"
                 "dsky_keypad_320x240.c" "dsky_render_320x240.c"
    INCLUDE_DIRS "include"
    REQUIRES     channel_router log esp_driver_gpio esp_driver_spi
)
