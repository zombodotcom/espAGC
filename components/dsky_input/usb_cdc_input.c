// usb_cdc_input.c
//
// On ESP32-C5 the USB-CDC interface is provided by the ROM USB-Serial-JTAG
// peripheral; no tinyusb stack is required. We register a small RX handler
// task that reads bytes via the IDF driver and forwards them through
// dsky_input_parse_token → channel_router_post_key.

#include "channel_router.h"
#include "dsky_input.h"

#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "usb_cdc";

static void rx_task(void *arg)
{
    (void)arg;
    uint8_t buf[64];
    for (;;) {
        int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(50));
        for (int i = 0; i < n; i++) {
            char c = (char)buf[i];
            if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
            char tok[2] = { c, 0 };
            int code = dsky_input_parse_token(tok);
            if (code >= 0) channel_router_post_key(code);
            else ESP_LOGW(TAG, "ignored char 0x%02x", (unsigned)c);
        }
    }
}

void usb_cdc_input_start(void)
{
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&cfg));
    xTaskCreate(rx_task, "usb_cdc_rx", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "USB-Serial-JTAG DSKY input ready");
}
