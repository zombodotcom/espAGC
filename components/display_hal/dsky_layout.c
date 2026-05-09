// components/display_hal/dsky_layout.c
//
// Resolution -> renderer lookup. Add new layouts here.

#include "dsky_layout.h"
#include <stddef.h>

extern const dsky_layout_t dsky_layout_320x240;

const dsky_layout_t *dsky_layout_for(int w, int h)
{
    if (w == 320 && h == 240) return &dsky_layout_320x240;
    return NULL;
}
