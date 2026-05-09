// components/led_status/include/led_status_iface.h
#pragma once
//
// led_status_iface_t — board-agnostic single-RGB-LED interface.
// Boards without an LED return NULL from board_get_led().

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*init)(void);
    void (*set_rgb)(uint8_t r, uint8_t g, uint8_t b);
} led_status_iface_t;

#ifdef __cplusplus
}
#endif
