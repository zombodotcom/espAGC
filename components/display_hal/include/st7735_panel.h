#pragma once
//
// ST7735 panel driver for the T-Dongle-C5's onboard 0.96" 80x160 LCD.
//
// This is a direct C port of LilyGO's Adafruit_ST7735-based reference
// (third_party/T-Dongle-C5/lib/lcd_st7735/st7735.cpp), originally ported
// in dosNew/esp-dos/firmware/components/display/st7735_panel.c. We use
// it instead of esp_lcd's panel API because the maintained
// waveshare/esp_lcd_st7735 managed component is broken on this exact
// panel (wrong INVON/INVOFF sequencing, wrong reset timing, wrong
// MADCTL on rotation 3).

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Native panel size in landscape (rotation 3).
#define DSKY_FB_W   160
#define DSKY_FB_H    80

esp_err_t st7735_init(void);

// Push a strip of the framebuffer (DSKY_FB_W wide x (y1-y0) tall, RGB565,
// host byte order). The driver byte-swaps to big-endian as it transmits.
esp_err_t st7735_draw_rows(int y0, int y1, const uint16_t *pixels);

#ifdef __cplusplus
}
#endif
