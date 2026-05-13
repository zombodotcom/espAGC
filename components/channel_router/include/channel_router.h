#pragma once
//
// channel_router — bridge between yaAGC IO channels and host-side abstractions.
//
//  AGC engine
//   │ ChannelOutput(ch, v)       channel_router_on_output(ch, v)
//   ▼                            ▼
//  io_callbacks.c ──────────────► channel_router ──► dsky_state_t snapshot
//                                                  (read by display_hal)
//
//  Keypress input flows directly into yaagc_socket's synthetic-client
//  byte ring; ChannelInput inside the engine drains it canonically.
//

#include <stdbool.h>
#include <stdint.h>

#include "dsky_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void channel_router_init(void);

// Called from io_callbacks (engine context).
void channel_router_on_output(int channel, int value);
void channel_router_on_routine(void);

// Called from the input transports (touch, WiFi, serial).
// `code` is a 5-bit AGC keypress code (see dsky_keys.h).
void channel_router_post_key(int code);

// Read a snapshot of the DSKY state. The function copies under a mutex; the
// caller can read freely. Returns the generation counter so the UI task can
// skip redraws when nothing has changed.
uint64_t channel_router_snapshot(dsky_state_t *out);

#ifdef __cplusplus
}
#endif
