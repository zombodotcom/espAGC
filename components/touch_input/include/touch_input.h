// components/touch_input/include/touch_input.h
#pragma once
//
// touch_input — board-agnostic touchscreen-to-DSKY-key bridge. The board
// provides a panel_touch_iface_t; the active dsky_layout provides the
// hit-test. touch_input owns a low-priority FreeRTOS task that polls
// the touch iface at 50 Hz and posts decoded keys into channel_router.

#include "panel_touch_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*touch_hit_test_fn)(int x, int y);

void touch_input_start(const panel_touch_iface_t *touch, touch_hit_test_fn hit_test);

#ifdef __cplusplus
}
#endif
