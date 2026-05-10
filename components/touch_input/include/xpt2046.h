// components/touch_input/include/xpt2046.h
#pragma once
//
// XPT2046 resistive touch controller driver. SPI-attached, 12-bit ADC,
// reports raw X/Y/pressure. Used by the canonical CYD (ESP32-2432S028)
// 2.8" board on its own SPI bus distinct from the LCD.

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int  host;          // SPIn_HOST (use SPI3_HOST since LCD is on SPI2_HOST)
    int  sck;
    int  mosi;
    int  miso;
    int  cs;
    int  irq;           // -1 if unused
    // Calibration: raw ADC range that maps to panel coords.
    // For the standard CYD landscape orientation these defaults are close.
    int  raw_x_min;     // default 200
    int  raw_x_max;     // default 3900
    int  raw_y_min;     // default 200
    int  raw_y_max;     // default 3900
    bool swap_xy;       // landscape orientation: native portrait → landscape
    bool invert_x;
    bool invert_y;
    int  panel_w;       // landscape pixel width  (320)
    int  panel_h;       // landscape pixel height (240)
} xpt2046_pins_t;

esp_err_t xpt2046_init_with_pins(const xpt2046_pins_t *p);

// Reads the controller; returns true if currently pressed and writes
// panel-coordinate (x,y) to *x_out / *y_out.
bool xpt2046_poll(int *x_out, int *y_out);

#ifdef __cplusplus
}
#endif
