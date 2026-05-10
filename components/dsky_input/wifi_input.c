// wifi_input.c
//
// Brings the dongle online and serves a small web DSKY at "/" with a
// virtual keypad. Each click POSTs a token to "/key", and the handler
// converts it to an AGC keycode via dsky_input_parse_token.
//
// By default we run in **STA mode** and join an existing network — that
// way the dongle keeps internet access for the rest of the LAN and shows
// up on the DHCP-assigned IP. If CONFIG_ESPAGC_WIFI_SSID is empty we
// fall back to a SoftAP called "espAGC" so first-time setup still works.

#include "channel_router.h"
#include "dsky_input.h"
#include "sequences.h"

#include <string.h>
#include <stdio.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

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

// GET /seqs -> JSON list of available canned sequences:
//   [{"i":0,"name":"...","desc":"..."}, ...]
static esp_err_t handle_seqs(httpd_req_t *req)
{
    char buf[1024];
    int  off = 0;
    off += snprintf(buf + off, sizeof(buf) - off, "[");
    for (int i = 0; i < sequences_count(); i++) {
        const sequence_t *s = sequences_get(i);
        off += snprintf(buf + off, sizeof(buf) - off,
                        "%s{\"i\":%d,\"name\":\"%s\",\"desc\":\"%s\"}",
                        i ? "," : "", i, s->name, s->description);
    }
    off += snprintf(buf + off, sizeof(buf) - off, "]");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, off);
}

// POST /seq -> body is the integer index of the sequence to run.
static esp_err_t handle_seq(httpd_req_t *req)
{
    char buf[16];
    int recv = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (recv <= 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body"); return ESP_FAIL; }
    buf[recv] = 0;
    int idx = atoi(buf);
    int rc  = sequences_run(idx);
    if (rc == 0)  return httpd_resp_sendstr(req, "ok");
    if (rc == -2) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_sendstr(req, "another sequence is still running");
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad index");
    return ESP_FAIL;
}

static void start_http(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));
    httpd_uri_t root = { .uri = "/",     .method = HTTP_GET,  .handler = handle_index };
    httpd_uri_t key  = { .uri = "/key",  .method = HTTP_POST, .handler = handle_key   };
    httpd_uri_t seqs = { .uri = "/seqs", .method = HTTP_GET,  .handler = handle_seqs  };
    httpd_uri_t seq  = { .uri = "/seq",  .method = HTTP_POST, .handler = handle_seq   };
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &key);
    httpd_register_uri_handler(server, &seqs);
    httpd_register_uri_handler(server, &seq);
}

// ---- STA path ----------------------------------------------------------

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)        esp_wifi_connect();
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "disconnected, retrying");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got ip: " IPSTR " — web DSKY at http://" IPSTR "/",
                 IP2STR(&e->ip_info.ip), IP2STR(&e->ip_info.ip));
        start_http();
    }
}

static void start_sta(const char *ssid, const char *pwd)
{
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,   IP_EVENT_STA_GOT_IP,
                                               &on_wifi_event, NULL));

    wifi_config_t cfg = { 0 };
    strncpy((char *)cfg.sta.ssid,     ssid, sizeof(cfg.sta.ssid)     - 1);
    strncpy((char *)cfg.sta.password, pwd,  sizeof(cfg.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "joining '%s' …", ssid);
}

// ---- AP fallback -------------------------------------------------------

static void start_ap(const char *ssid, const char *password)
{
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
    ESP_LOGI(TAG, "SoftAP fallback '%s' up; web DSKY at http://192.168.4.1/", ssid);
}

void wifi_input_start(const char *ssid_arg, const char *password_arg)
{
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

#ifdef CONFIG_ESPAGC_WIFI_MODE_STA
    const char *ssid = CONFIG_ESPAGC_WIFI_SSID;
    const char *pwd  = CONFIG_ESPAGC_WIFI_PASSWORD;
    if (ssid && ssid[0]) {
        start_sta(ssid, pwd);
        return;
    }
    ESP_LOGW(TAG, "STA mode requested but ESPAGC_WIFI_SSID is empty; "
                  "falling back to SoftAP. Run `idf.py menuconfig` and set "
                  "espAGC WiFi → WiFi SSID/Password.");
#endif

    const char *ap_ssid = (ssid_arg && ssid_arg[0]) ? ssid_arg : "espAGC";
    const char *ap_pwd  = password_arg ? password_arg : "";
    start_ap(ap_ssid, ap_pwd);
}
