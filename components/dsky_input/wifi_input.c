// wifi_input.c
//
// Brings up a SoftAP, serves a static SPA at "/", and accepts DSKY keypresses
// at "POST /key" (request body is a single ASCII token like "VRB", "1", "+").
// HTTPD WebSocket support requires CONFIG_HTTPD_WS_SUPPORT=y which is opt-in;
// plain POST is simpler, works without that flag, and keeps the SPA short.

#include "channel_router.h"
#include "dsky_input.h"

#include <string.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_input";

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

static esp_err_t handle_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)index_html_start,
                           index_html_end - index_html_start);
}

static esp_err_t handle_key(httpd_req_t *req)
{
    char buf[16];
    int recv = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (recv <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body");
        return ESP_FAIL;
    }
    buf[recv] = 0;

    int code = dsky_input_parse_token(buf);
    if (code < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unknown token");
        return ESP_FAIL;
    }
    channel_router_post_key(code);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "ok");
}

static void start_http(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));

    httpd_uri_t root = { .uri = "/",   .method = HTTP_GET,  .handler = handle_index };
    httpd_uri_t key  = { .uri = "/key",.method = HTTP_POST, .handler = handle_key   };
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &key);
}

void wifi_input_start(const char *ssid, const char *password)
{
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));

    wifi_config_t apc = { 0 };
    strncpy((char *)apc.ap.ssid, ssid, sizeof(apc.ap.ssid) - 1);
    apc.ap.ssid_len = strlen(ssid);
    apc.ap.max_connection = 4;
    if (password && password[0]) {
        strncpy((char *)apc.ap.password, password, sizeof(apc.ap.password) - 1);
        apc.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        apc.ap.authmode = WIFI_AUTH_OPEN;
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &apc));
    ESP_ERROR_CHECK(esp_wifi_start());

    start_http();
    ESP_LOGI(TAG, "WiFi AP '%s' up; web DSKY at http://192.168.4.1/", ssid);
}
