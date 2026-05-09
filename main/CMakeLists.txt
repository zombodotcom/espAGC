idf_component_register(
    SRCS         "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES     agc_core apollo_rom channel_router display_hal dsky_input
)
