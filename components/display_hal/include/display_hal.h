#pragma once
//
// display_hal — opaque interface between channel_router's dsky_state_t and
// whatever physical panel is on the device. The default backend in this tree
// drives an ST7735 80x160 LCD via LVGL; new panels (240x240 GC9A01, parallel
// IPS, e-ink) drop in as additional panel_*.c files implementing
// display_hal_panel_iface_t below.

#include "dsky_state.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the configured panel and the LVGL DSKY widgets.
void display_hal_init(void);

// Apply a fresh dsky_state snapshot. Safe to call from the UI task.
void display_hal_update(const dsky_state_t *state);

#ifdef __cplusplus
}
#endif
