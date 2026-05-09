#pragma once
//
// dsky_input — input transports that feed channel_router_post_key().
//
//  WiFi WS ──┐
//  Touch ────┼─► key_parse ─► channel_router_post_key(code5)
//  Boot btn ─┘
//
// (Touch lives in components/touch_input. WiFi web DSKY lives here.)

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool enable_wifi_ap;
    const char *wifi_ssid;        // NULL → "espAGC"
    const char *wifi_password;    // NULL or "" → open network
} dsky_input_config_t;

void dsky_input_start(const dsky_input_config_t *cfg);

// Parses a single ASCII keypress (or short token like "VRB", "PRO") into an
// AGC 5-bit keycode. Returns -1 if the token is not recognised.
int dsky_input_parse_token(const char *token);

#ifdef __cplusplus
}
#endif
