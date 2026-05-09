idf_component_register(
    SRCS         "dsky_input.c" "usb_cdc_input.c" "wifi_input.c" "key_parse.c"
    INCLUDE_DIRS "include"
    EMBED_TXTFILES "web/index.html"
    REQUIRES     channel_router esp_http_server esp_wifi nvs_flash esp_event esp_netif esp_driver_usb_serial_jtag
    PRIV_REQUIRES esp_driver_gpio
)
