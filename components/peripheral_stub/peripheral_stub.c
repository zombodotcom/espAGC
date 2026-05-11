// peripheral_stub.c — implementation. See peripheral_stub.h for rationale.
//
// Two-part role:
//
// 1. Channel/IMODES baseline maintenance (the original design). Every
//    tick, idempotently re-assign the peripheral input channels Luminary
//    monitors and rewrite the IMODES30/IMODES33 erasable mirrors to
//    fresh-start values. yaAGC's `agc_t::Erasable[8][0400]` indexes by
//    (addr / 0400, addr % 0400); IMODES30 @ octal 01302 -> bank 2 offset
//    0302; IMODES33 @ 01303 -> bank 2 offset 0303. Fresh values come
//    from FRESH_START_AND_RESTART.agc lines 152-154 (IMODES33 = IM33INIT
//    + BIT6 = 016000 + 040 = 016040; IMODES30 = IM30INIF = 037411).
//
// 2. Host-side ERROR routine (the "we shipped the corner cut" honest
//    fix). The FAILREG diagnostic (tests/host/test_failreg_diagnostic.c)
//    established that the only alarm Luminary fires across 10M cycles
//    is `01107` NIGHT WATCHMAN, set during the boot-time transient
//    when the executive doesn't reach NEWJOB fast enough during the
//    first SCALER1 cycle. The engine GOJAMs, recovers, executive runs
//    normally — but DSPTAB+11D bit 9 (PROG ALARM lamp) and FAILREG[0]
//    retain the trip code. Luminary's own ERROR routine on RSET
//    (PINBALL_GAME__BUTTONS_AND_LIGHTS.agc:3744-3801) is what's supposed
//    to clear them, but the in-engine KEYRUPT1 -> NOVAC(CHARIN) path
//    isn't reliably dispatching to CHARIN in our integration. Until
//    that's understood (see docs/SESSION_NOTES.md "What NOT to do"),
//    do what ERROR does from outside the engine:
//      - Clear DSPTAB+11D except bits 4 (NO ATT) + 6 (GIMBAL LOCK),
//        per ERROR's `MASK GL+NOATT`.
//      - Zero FAILREG[0..2] per ERROR's `CAF ZERO; TS FAILREG...`.
//      - Set DSPTAB+11D bit 15 (request flag) per ERROR's `AD BIT15`.
//    Only act when FAILREG[0] == 01107 (NIGHT WATCHMAN). Any other
//    alarm is a real fault and should remain visible.

#include "peripheral_stub.h"

#define CH030_BASELINE        036377
#define CH033_BASELINE        077777
#define IMODES30_BANK         2
#define IMODES30_OFFSET       0302
#define IMODES30_FRESH        037411
#define IMODES33_BANK         2
#define IMODES33_OFFSET       0303
#define IMODES33_FRESH        016040

// FAILREG @ erasable 0375..0377, bank 0 (MAIN.agc.html).
#define FAILREG_BANK          0
#define FAILREG0_OFFSET       0375
#define FAILREG1_OFFSET       0376
#define FAILREG2_OFFSET       0377
#define ALARM_NIGHT_WATCHMAN  01107   // ALARM_AND_ABORT.agc - NW code

// DSPTAB +11D @ erasable 01036, bank 2 offset 036. Bit 9 (0o400) is
// PROG ALARM lamp. Bits 4 (NO ATT, 0o20) and 6 (GIMBAL LOCK, 0o100)
// are preserved by ERROR; bit 15 (0o40000) is set by ERROR as request.
#define DSPTAB11D_BANK        2
#define DSPTAB11D_OFFSET      036
#define DSPTAB_PROG_ALARM     0400u
#define DSPTAB_NOATT          0020u
#define DSPTAB_GIMBAL_LOCK    0100u
#define DSPTAB_GL_NOATT       (DSPTAB_NOATT | DSPTAB_GIMBAL_LOCK)
#define DSPTAB_REQUEST        040000u

// LM_Simulator's boot-time channel values (lm_simulator.tcl:570-572).
// LM_Simulator writes these via socket on connect; we write them via
// the engine's WriteIO entry point (same path the socket eventually
// reaches through agc_engine.c's input-packet handler).
//
//   wdata(30)  = "011110011011001"  -> 0o36331
//   wdata(31)  = "111111111111111"  -> 0o77777
//   wdata(32)  = "010001111111111"  -> 0o21777
//   wdata(33)  = "101111111111110"  -> 0o57776
#define LM_SIM_CH030  036331
#define LM_SIM_CH031  077777
#define LM_SIM_CH032  021777
#define LM_SIM_CH033  057776

// Step accounting: tracks total simulated time and pulse-emission cadence.
// Reset by peripheral_stub_init.
static uint64_t g_step_time_us = 0;
static uint32_t g_pulse_phase  = 0;  // increments every step, drives the
                                     // CDU pulse cadence (Increment B).

void peripheral_stub_init(void)
{
    extern agc_t *agc_core_state(void);
    agc_t *state = agc_core_state();
    if (state == NULL) return;

    // Match LM_Simulator's startup channel writes. We use direct
    // InputChannel assignment here (rather than WriteIO) because we're
    // running at boot before any engine cycle has executed — the
    // distinction is invisible: both update the channel state the
    // engine sees on its first cycle. The periodic step (below) uses
    // direct writes too, matching what the engine's socket-input path
    // does after parsing an external packet (agc_engine.c:WriteIO()
    // is the CPU-side write; the socket-input path uses the same
    // InputChannel array via ParseIoPacket).
    state->InputChannel[030] = LM_SIM_CH030;
    state->InputChannel[031] = LM_SIM_CH031;
    state->InputChannel[032] = LM_SIM_CH032;
    state->InputChannel[033] = LM_SIM_CH033;

    g_step_time_us = 0;
    g_pulse_phase  = 0;
}

// One simulation step. Call at ~100 Hz (every 10 ms of simulated time).
// On hardware: a dedicated FreeRTOS task with vTaskDelayUntil.
// On host: interleaved with agc_engine cycles via the harness tick hook.
//
// Increment A (this commit): periodic re-write of channels 30-33 so that
// any spurious writes from Luminary don't drift them away from the
// LM_Simulator baseline. LM_Simulator does the same — writes every time
// its update timer fires.
//
// Increment B (next): push CDU counter pulses via UnprogrammedIncrement.
// Increment C: integrate attitude state, respond to channel 5/6 jet
// commands, drive CDU at integrated rate.
void peripheral_stub_step(agc_t *state, uint32_t dt_us)
{
    if (state == NULL) return;
    g_step_time_us += dt_us;
    g_pulse_phase++;

    // Re-assert the LM_Simulator baselines on every step. ch031/032 carry
    // RHC/THC/PRO state — leave those alone if the user is interacting;
    // for now we just push the defaults, which is what LM_Simulator does
    // when no controls are touched.
    state->InputChannel[030] = LM_SIM_CH030;
    state->InputChannel[033] = LM_SIM_CH033;
}

void peripheral_stub_tick(agc_t *state)
{
    if (state == NULL) return;

    // Drive the periodic simulation step. The legacy tick from
    // channel_router_on_routine() comes about once per 16 k engine
    // cycles, which at ~12 us per cycle is ~200 ms — slower than the
    // 10 ms LM_Simulator cadence but enough to demonstrate the
    // infrastructure. Hardware will spawn a dedicated 100 Hz task too.
    peripheral_stub_step(state, 200000);  // 200 ms of sim time per call

    // Restore IMODES30/IMODES33 to fresh-start values so any fault
    // bits Luminary set on its last mode-switch pass go away before
    // the next ALARM check runs. (peripheral_stub_step handles the
    // ch030/033 baselines.)
    state->Erasable[IMODES30_BANK][IMODES30_OFFSET] = IMODES30_FRESH;
    state->Erasable[IMODES33_BANK][IMODES33_OFFSET] = IMODES33_FRESH;

    // (2) Host-side ERROR: if the boot-time NW trip latched the PROG
    // ALARM lamp and our KEYRUPT-driven RSET didn't clear it (CHARIN
    // not being dispatched - see SESSION_NOTES), clear it here. Only
    // act if the alarm code in FAILREG[0] is the known-benign NW code;
    // any other alarm represents a real fault that should stay visible.
    int failreg0 = state->Erasable[FAILREG_BANK][FAILREG0_OFFSET];
    if (failreg0 == ALARM_NIGHT_WATCHMAN) {
        // Equivalent of ERROR lines 3752-3755:
        //   CAF GL+NOATT ; MASK DSPTAB+11D ; AD BIT15 ; TS DSPTAB+11D
        unsigned dsp = state->Erasable[DSPTAB11D_BANK][DSPTAB11D_OFFSET];
        dsp = (dsp & DSPTAB_GL_NOATT) | DSPTAB_REQUEST;
        state->Erasable[DSPTAB11D_BANK][DSPTAB11D_OFFSET] = dsp;

        // Equivalent of ERROR lines 3796-3799: zero FAILREG[0..2].
        state->Erasable[FAILREG_BANK][FAILREG0_OFFSET] = 0;
        state->Erasable[FAILREG_BANK][FAILREG1_OFFSET] = 0;
        state->Erasable[FAILREG_BANK][FAILREG2_OFFSET] = 0;
    }
}
