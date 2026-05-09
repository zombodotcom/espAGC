// components/touch_input/include/panel_touch_iface.h
#pragma once
//
// panel_touch_iface_t — board-agnostic touchscreen interface.
// Boards without a touchscreen return NULL from board_get_touch().

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_err_t (*init)(void);
    bool (*poll)(int *x, int *y);   // true if pressed; (x,y) in panel coords
} panel_touch_iface_t;

#ifdef __cplusplus
}
#endif
