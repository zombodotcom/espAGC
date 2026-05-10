#pragma once
//
// Canned DSKY key sequences. Each sequence is a name + an ordered list of
// AGC 5-bit keycodes; a runner task plays them back into channel_router with
// a ~250 ms gap between keys so the AGC's PINBALL keypress logic has time to
// process each one. Only one sequence runs at a time.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char    *name;
    const char    *description;
    const uint8_t *keys;        // array of dsky_key_t codes (5 bits each)
    int            key_count;
} sequence_t;

int                 sequences_count(void);
const sequence_t   *sequences_get(int index);

// Spawn a runner task that injects sequences_get(index)->keys into
// channel_router_post_key() with a fixed delay between presses. Returns
// negative if the index is bad or another sequence is already running.
int                 sequences_run(int index);

#ifdef __cplusplus
}
#endif
