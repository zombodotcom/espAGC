#pragma once
//
// Layer-2 host harness: wires real agc_core + real channel_router (compiled
// from components/, not stubbed) so tests can boot Luminary099 and observe
// the same dsky_state_t the renderer would render on hardware.

#include <stdbool.h>
#include <stdint.h>
#include "dsky_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool rupt_lock;
    bool night_watchman_tripped;
    bool tc_trap;
    bool no_tc;
    bool parity_fail;
    bool warning_filter_active;
    bool generated_warning;
} harness_alarms_t;

// Boot the engine with the ROM at $ROM (env var). Aborts on failure.
void harness_boot(void);

// Run N AGC engine cycles in batches; nothing concurrent here, so cycles
// turn into channel writes synchronously into channel_router.
void harness_step(int n_cycles);

// Inject a DSKY keypress (5-bit code; see dsky_keys.h).
void harness_post_key(int code);

// Snapshot the resolved DSKY state from channel_router.
void harness_snapshot(dsky_state_t *out);

// Read the agc_engine internal alarm flags directly.
void harness_alarms(harness_alarms_t *out);

// Convenience: type out a string of DSKY tokens. Whitespace is ignored.
//   "V37E00E" -> VERB, 3, 7, ENTR, 0, 0, ENTR
// Each key advances the engine by `gap_cycles` cycles before the next.
void harness_type(const char *seq, int gap_cycles);

#ifdef __cplusplus
}
#endif
