#pragma once
//
// channel_router — bridge between yaAGC IO channels and host-side abstractions.
//
//  AGC engine
//   │ ChannelOutput(ch, v)       channel_router_on_output(ch, v)
//   ▼                            ▼
//  io_callbacks.c ──────────────► channel_router ──► dsky_state_t snapshot
//   ▲                            ▲                 (read by display_hal)
//   │ ChannelInput(state)        ▲
//   │                            │
//  io_callbacks.c ◄────── channel_router_pump_input(state)
//                                ▲
//                                │ dsky_input_post_key(code)
//                                │
//                       USB-CDC and WiFi transports
//

#include <stdbool.h>
#include <stdint.h>

#include "dsky_state.h"

// agc_t is treated as opaque in this header — io_callbacks.c (which calls
// channel_router_pump_input) is the only place that needs the real type, and
// it includes agc_engine.h itself.

#ifdef __cplusplus
extern "C" {
#endif

void channel_router_init(void);

// Called from io_callbacks (engine context).
void channel_router_on_output(int channel, int value);
int  channel_router_pump_input(void *agc_state);
void channel_router_on_routine(void);

// Called from the input transports (USB, WiFi, button).
// `code` is a 5-bit AGC keypress code (see dsky_keys.h).
void channel_router_post_key(int code);

// Read a snapshot of the DSKY state. The function copies under a mutex; the
// caller can read freely. Returns the generation counter so the UI task can
// skip redraws when nothing has changed.
uint64_t channel_router_snapshot(dsky_state_t *out);

#ifdef __cplusplus
}
#endif
