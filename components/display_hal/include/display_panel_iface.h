// components/display_hal/include/display_panel_iface.h
#pragma once
//
// display_panel_iface_t — board-agnostic LCD interface used by display_hal.
// Each board component returns a pointer to a static instance via
// board_get_panel(). The renderer pushes RGB565 strips through draw_rows;
// the panel impl handles byte order and address-window setup.

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int       width;
    int       height;
    bool      swap_bytes;
    esp_err_t (*init)(void);
    esp_err_t (*draw_rows)(int y0, int y1, const uint16_t *pixels);
} display_panel_iface_t;

#ifdef __cplusplus
}
#endif
