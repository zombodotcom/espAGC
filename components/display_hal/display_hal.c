// display_hal.c — placeholder console-only backend.
//
// Logs the DSKY snapshot over USB-Serial-JTAG / UART once per second and
// whenever any field changes. The "real" backend is an ST7735 + LVGL DSKY
// rendered on the T-Dongle-C5's onboard panel; that work is queued for
// re-enabling after Layer 1 (host tests) and Layer 3 (hardware bring-up)
// are both green. Reference driver: dosNew/esp-dos/firmware/components/
// display/st7735_panel.c.

#include "display_hal.h"

#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "dsky";

static char digit(dsky_digit_t d) { return (d < 0) ? ' ' : (char)('0' + d); }
static char sign(dsky_sign_t s)   {
    return s == DSKY_SIGN_PLUS ? '+' : s == DSKY_SIGN_MINUS ? '-' : ' ';
}

void display_hal_init(void) { ESP_LOGI(TAG, "console DSKY backend ready"); }

void display_hal_update(const dsky_state_t *s)
{
    ESP_LOGI(TAG,
             "PRG %c%c VRB %c%c NUN %c%c | R1 %c%c%c%c%c%c R2 %c%c%c%c%c%c R3 %c%c%c%c%c%c | %s%s%s%s",
             digit(s->prog[0]), digit(s->prog[1]),
             digit(s->verb[0]), digit(s->verb[1]),
             digit(s->noun[0]), digit(s->noun[1]),
             sign(s->r1_sign), digit(s->r1[0]), digit(s->r1[1]), digit(s->r1[2]), digit(s->r1[3]), digit(s->r1[4]),
             sign(s->r2_sign), digit(s->r2[0]), digit(s->r2[1]), digit(s->r2[2]), digit(s->r2[3]), digit(s->r2[4]),
             sign(s->r3_sign), digit(s->r3[0]), digit(s->r3[1]), digit(s->r3[2]), digit(s->r3[3]), digit(s->r3[4]),
             s->comp_acty   ? "CA "  : "",
             s->uplink_acty ? "UP "  : "",
             s->prog_alarm  ? "PA "  : "",
             s->stby        ? "SBY"  : "");
}
