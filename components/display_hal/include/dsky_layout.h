// components/display_hal/include/dsky_layout.h
#pragma once
//
// dsky_layout_t — resolution-keyed DSKY renderer. display_hal looks one up
// via dsky_layout_for(panel_w, panel_h) and renders the framebuffer in
// strip_h-row passes.

#include "dsky_state.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int  fb_w;
    int  fb_h;
    int  strip_h;             // divides fb_h evenly
    void (*init_strip)(uint16_t *strip, int y0);
    void (*render_strip)(uint16_t *strip, const dsky_state_t *s, int y0);
    int  (*hit_test)(int x, int y);   // -1 if outside any button; NULL = no touch
} dsky_layout_t;

const dsky_layout_t *dsky_layout_for(int w, int h);

#ifdef __cplusplus
}
#endif
