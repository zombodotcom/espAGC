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
#include "dsky_state.h"
#include "peripheral_stub.h"
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
//
// Stream-chunked: each entry is sent separately so adding long verbose
// descriptions (e.g. the V71 Apollo 11 state-vector sequence) doesn't
// silently truncate the JSON. Previously a 1 KB stack buffer was used,
// and snprintf would write past the end as off advanced beyond
// sizeof(buf), producing a corrupt response and a JSON.parse error
// in the browser at column ~1016.
static esp_err_t handle_seqs(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send_chunk(req, "[", 1);
    char entry[512];
    for (int i = 0; i < sequences_count(); i++) {
        const sequence_t *s = sequences_get(i);
        int n = snprintf(entry, sizeof(entry),
                         "%s{\"i\":%d,\"name\":\"%s\",\"desc\":\"%s\"}",
                         i ? "," : "", i, s->name, s->description);
        if (n < 0) n = 0;
        if (n > (int)sizeof(entry) - 1) n = (int)sizeof(entry) - 1;
        httpd_resp_send_chunk(req, entry, n);
    }
    httpd_resp_send_chunk(req, "]", 1);
    return httpd_resp_send_chunk(req, NULL, 0);   // end of chunks
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

// GET /state -> JSON snapshot of the resolved DSKY state so the web UI
// can render the same panel the ST7789 shows on the device. dsky_digit_t
// uses -1 for blank; emit those as the JSON string "_" so the browser
// can render them as the dimmed-segment blank position.
static void append_digits(char *buf, size_t cap, size_t *off,
                          const char *name, const dsky_digit_t *d, int n,
                          dsky_sign_t sign)
{
    *off += snprintf(buf + *off, cap - *off, ",\"%s\":\"", name);
    if (sign == DSKY_SIGN_PLUS)  *off += snprintf(buf + *off, cap - *off, "+");
    else if (sign == DSKY_SIGN_MINUS) *off += snprintf(buf + *off, cap - *off, "-");
    for (int i = 0; i < n; i++) {
        if (d[i] == DSKY_BLANK) *off += snprintf(buf + *off, cap - *off, "_");
        else                    *off += snprintf(buf + *off, cap - *off, "%d", d[i]);
    }
    *off += snprintf(buf + *off, cap - *off, "\"");
}

static esp_err_t handle_state(httpd_req_t *req)
{
    dsky_state_t s;
    channel_router_snapshot(&s);
    char buf[768];
    size_t off = 0;
    off += snprintf(buf + off, sizeof(buf) - off,
                    "{\"gen\":%llu", (unsigned long long)s.generation);
    append_digits(buf, sizeof(buf), &off, "prog", s.prog, 2, DSKY_SIGN_NONE);
    append_digits(buf, sizeof(buf), &off, "verb", s.verb, 2, DSKY_SIGN_NONE);
    append_digits(buf, sizeof(buf), &off, "noun", s.noun, 2, DSKY_SIGN_NONE);
    append_digits(buf, sizeof(buf), &off, "r1",   s.r1,   5, s.r1_sign);
    append_digits(buf, sizeof(buf), &off, "r2",   s.r2,   5, s.r2_sign);
    append_digits(buf, sizeof(buf), &off, "r3",   s.r3,   5, s.r3_sign);
    off += snprintf(buf + off, sizeof(buf) - off,
        ",\"ca\":%d,\"up\":%d,\"temp\":%d,\"noatt\":%d,\"gl\":%d,"
        "\"pa\":%d,\"rstr\":%d,\"trk\":%d,\"krel\":%d,\"oe\":%d,"
        "\"stby\":%d,\"fvn\":%d}",
        s.comp_acty, s.uplink_acty, s.temp, s.no_att, s.gimbal_lock,
        s.prog_alarm, s.restart, s.tracker, s.key_rel, s.opr_err,
        s.stby, s.flash_verb_noun);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, off);
}

// POST /thrust -> body "0" or "1" toggles the descent-thrust PIPA driver
// in peripheral_stub. Useful for demonstrating P63/P66 with HDOT
// counting up. See peripheral_stub_set_descent_thrust comment.
static esp_err_t handle_thrust(httpd_req_t *req)
{
    char buf[8];
    int recv = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (recv <= 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body"); return ESP_FAIL; }
    buf[recv] = 0;
    peripheral_stub_set_descent_thrust(atoi(buf));
    return httpd_resp_sendstr(req, "ok");
}

static void start_http(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));
    httpd_uri_t root   = { .uri = "/",       .method = HTTP_GET,  .handler = handle_index  };
    httpd_uri_t key    = { .uri = "/key",    .method = HTTP_POST, .handler = handle_key    };
    httpd_uri_t seqs   = { .uri = "/seqs",   .method = HTTP_GET,  .handler = handle_seqs   };
    httpd_uri_t seq    = { .uri = "/seq",    .method = HTTP_POST, .handler = handle_seq    };
    httpd_uri_t state  = { .uri = "/state",  .method = HTTP_GET,  .handler = handle_state  };
    httpd_uri_t thrust = { .uri = "/thrust", .method = HTTP_POST, .handler = handle_thrust };
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &key);
    httpd_register_uri_handler(server, &seqs);
    httpd_register_uri_handler(server, &seq);
    httpd_register_uri_handler(server, &state);
    httpd_register_uri_handler(server, &thrust);
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
