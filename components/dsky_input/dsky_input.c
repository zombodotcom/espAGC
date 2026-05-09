#include "dsky_input.h"

#include "esp_log.h"

extern void wifi_input_start(const char *ssid, const char *password);

static const char *TAG = "dsky_input";

void dsky_input_start(const dsky_input_config_t *cfg)
{
    if (cfg && cfg->enable_wifi_ap) {
        const char *ssid = cfg->wifi_ssid     ? cfg->wifi_ssid     : "espAGC";
        const char *pwd  = cfg->wifi_password ? cfg->wifi_password : "";
        wifi_input_start(ssid, pwd);
    }
    ESP_LOGI(TAG, "input transports: wifi_ap=%d",
             cfg ? cfg->enable_wifi_ap : 0);
}
