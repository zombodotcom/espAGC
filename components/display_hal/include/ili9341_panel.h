// components/display_hal/include/ili9341_panel.h
#pragma once
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int sck, mosi, miso, cs, dc, rst, bl;
} ili9341_pins_t;

esp_err_t ili9341_init(const ili9341_pins_t *pins);
esp_err_t ili9341_draw_rows(int y0, int y1, const uint16_t *pixels);

#ifdef __cplusplus
}
#endif
