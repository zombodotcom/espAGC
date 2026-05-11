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

// Tick the peripheral stub once. On hardware this is called from the agc
// task at ~100Hz to refresh ch030/ch033 baselines and clear stale IMODES.
// In host tests, call this explicitly to mimic hardware-side environment.
void harness_tick_peripherals(void);

// Enable auto-tick: peripheral_stub_tick() is called every N engine cycles.
// Pass 0 to disable. Default is 0 (off) so existing tests are unchanged.
void harness_set_peripheral_tick_interval(int cycles);

// Inject a DSKY keypress (5-bit code; see dsky_keys.h).
void harness_post_key(int code);

// Snapshot the resolved DSKY state from channel_router.
void harness_snapshot(dsky_state_t *out);

// Read the agc_engine internal alarm flags directly.
void harness_alarms(harness_alarms_t *out);

// Read Luminary's FAILREG alarm-code FIFO. Per ALARM_AND_ABORT.agc:71-86,
// the ALARM routine stores the alarm code in FAILREG[0] on first alarm,
// FAILREG[1] on second distinct alarm, FAILREG[2] on third. PROGLARM
// (DSPTAB+11D bit 9) is set only by the first-alarm path; subsequent
// alarms just stack codes without re-lighting the lamp.
//
// FAILREG sits at erasable address octal 0375 (bank 0, offset 0375 per
// MAIN.agc.html symtab). FAILREG +1 at 0376, FAILREG +2 at 0377.
typedef struct {
    int latest;       // FAILREG[0] = state->Erasable[0][0375]
    int second;       // FAILREG[1] = state->Erasable[0][0376]
    int third;        // FAILREG[2] = state->Erasable[0][0377]
} harness_failreg_t;

void harness_failreg(harness_failreg_t *out);

// Convenience: type out a string of DSKY tokens. Whitespace is ignored.
//   "V37E00E" -> VERB, 3, 7, ENTR, 0, 0, ENTR
// Each key advances the engine by `gap_cycles` cycles before the next.
void harness_type(const char *seq, int gap_cycles);

#ifdef __cplusplus
}
#endif
