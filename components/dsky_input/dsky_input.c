#include "dsky_input.h"

#include "esp_log.h"

extern void usb_cdc_input_start(void);
extern void wifi_input_start(const char *ssid, const char *password);

static const char *TAG = "dsky_input";

void dsky_input_start(const dsky_input_config_t *cfg)
{
    if (cfg && cfg->enable_usb_cdc) {
        usb_cdc_input_start();
    }
    if (cfg && cfg->enable_wifi_ap) {
        const char *ssid = cfg->wifi_ssid     ? cfg->wifi_ssid     : "espAGC";
        const char *pwd  = cfg->wifi_password ? cfg->wifi_password : "";
        wifi_input_start(ssid, pwd);
    }
    ESP_LOGI(TAG, "input transports: usb=%d wifi_ap=%d",
             cfg ? cfg->enable_usb_cdc : 0,
             cfg ? cfg->enable_wifi_ap : 0);
}
