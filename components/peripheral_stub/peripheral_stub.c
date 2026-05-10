// peripheral_stub.c — implementation. See peripheral_stub.h for rationale.
//
// Approach: every tick, idempotently re-assign the peripheral input
// channels Luminary monitors, and rewrite the IMODES30/IMODES33
// erasable mirrors to fresh-start values. Luminary's T4JOB will pick
// these up on its next mode-switch cycle and find no faults.
//
// Address derivation: MAIN.agc.html shows IMODES30 @ octal 01302,
// IMODES33 @ 01303. yaAGC's agc_t::Erasable[8][0400] indexes by
// (addr / 0400, addr % 0400) -> bank 2, offsets 0302 / 0303.
//
// Fresh-start values come from FRESH_START_AND_RESTART.agc lines 152-154:
//   IMODES30 = 037411 (= IM30INIT per T4RUPT_PROGRAM.agc line 273)
//   IMODES33 = 016040 (= IM33INIT + BIT6 = 016000 + 040; BIT6 clears DAP/
//   error-needles display until ICDU zero is finished)
//
// Channel baselines match agc_init.c::init_cpu_state():
//   ch030 = 036377 (healthy LM)
//   ch033 = 077777 (no AGC warning)

#include "peripheral_stub.h"

#define CH030_BASELINE   036377
#define CH033_BASELINE   077777
#define IMODES30_BANK    2
#define IMODES30_OFFSET  0302
#define IMODES30_FRESH   037411
#define IMODES33_BANK    2
#define IMODES33_OFFSET  0303
#define IMODES33_FRESH   016040

void peripheral_stub_init(void)
{
    // Nothing to do at init; tick is idempotent and self-correcting.
}

void peripheral_stub_tick(agc_t *state)
{
    if (state == NULL) return;

    // Restore peripheral channel baselines (ch031 and ch032 carry
    // stick / PRO-key state and are intentionally left alone).
    state->InputChannel[030] = CH030_BASELINE;
    state->InputChannel[033] = CH033_BASELINE;

    // Restore IMODES30/IMODES33 to fresh-start values so any fault
    // bits Luminary set on its last mode-switch pass go away before
    // the next ALARM check runs.
    state->Erasable[IMODES30_BANK][IMODES30_OFFSET] = IMODES30_FRESH;
    state->Erasable[IMODES33_BANK][IMODES33_OFFSET] = IMODES33_FRESH;
}
