// components/display_hal/include/st7789_panel.h
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int  sck, mosi, miso, cs, dc, rst, bl;
    bool bl_active_low;     // false = drive bl high to turn on (canonical CYD2USB).
} st7789_pins_t;

esp_err_t st7789_init(const st7789_pins_t *pins);
esp_err_t st7789_draw_rows(int y0, int y1, const uint16_t *pixels);

#ifdef __cplusplus
}
#endif
