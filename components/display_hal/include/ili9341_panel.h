// components/display_hal/include/ili9341_panel.h
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int  sck, mosi, miso, cs, dc, rst, bl;
    bool bl_active_low;     // false = drive bl high to turn on (default CYD-C r1.x);
                            // true  = drive bl low  to turn on (some CYD-C revisions)
} ili9341_pins_t;

esp_err_t ili9341_init(const ili9341_pins_t *pins);
esp_err_t ili9341_draw_rows(int y0, int y1, const uint16_t *pixels);

#ifdef __cplusplus
}
#endif
