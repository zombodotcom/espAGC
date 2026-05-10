#pragma once
// peripheral_stub — watchdog for Luminary099 peripheral inputs.
//
// Luminary's IMU monitoring code (T4RUPT_PROGRAM.agc::T4JOB) reads
// channels 030/033 every mode-switch cycle, mirrors them into the
// IMODES30/IMODES33 erasable variables, and asserts PROG ALARM if any
// fault bits show. Without simulated CDU counters / radar this happens
// constantly. peripheral_stub_tick() restores those channels to
// healthy baselines and rewrites IMODES30/33 with fresh-start values
// on every tick, breaking the alarm-ack loop at idle.
//
// Called from channel_router_on_routine() at ~10 Hz wall-time.
//
// This is option (b) from
//   docs/superpowers/specs/2026-05-10-prog-alarm-watchdog-design.md
// Option (c) — actual CDU/radar simulation needed to run P63 descent —
// is deferred.

#include "yaAGC.h"
#include "agc_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

void peripheral_stub_init(void);
void peripheral_stub_tick(agc_t *state);

#ifdef __cplusplus
}
#endif
