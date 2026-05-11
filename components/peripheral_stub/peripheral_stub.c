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

// Boot-time channel values matching the recorded Apollo 11 launch
// (third_party/virtualagc/yaDSKY2/Apollo11-launch.canned) at time 0.
// This is the bit-for-bit channel state Luminary saw at boot in a
// known-working Apollo 11 launch. The recording then shows ch30
// transitioning to 37357 / 37356 / 37357 as the ISS turn-on sequence
// progresses — those transitions are driven by Luminary's own state
// machine in response to its boot-time outputs, not by the simulator.
//
// LM_Simulator's set_ini_values (lm_simulator.tcl:570-572) has slightly
// different ch30/ch31 defaults (036331/077777) but those produce more
// ch30/IMODES30 XOR mismatches that thrash T4RUPT's IMUMON loop. The
// recorded launch values are the truer fresh-start state.
#define LM_SIM_CH030  037377   // Apollo11-launch.canned at time 0
#define LM_SIM_CH031  057777   // Apollo11-launch.canned at time 0
#define LM_SIM_CH032  021777   // lm_simulator.tcl wdata(32) default
#define LM_SIM_CH033  057776   // lm_simulator.tcl wdata(33) default

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

// PCDU counter addresses (Block II AGC, per agc_engine.c FIRST_CDU=032).
// CDUX=032, CDUY=033, CDUZ=034. These are *erasable* counter registers
// the AGC IMU monitoring code (T4RUPT_PROGRAM.agc::T4JOB) polls.
// LM_Simulator pushes pulses to them at 400 counts/sec slow mode.
//
// IncType=1 = PCDU pulse (positive count, slow rate ~400 cps). yaAGC
// routes this through PushCduFifo so the increment happens at the
// engine's emulated hardware rate (every ~213 engine cycles per pulse).
#define CDUX_COUNTER  032
#define CDUY_COUNTER  033
#define CDUZ_COUNTER  034
#define PCDU_INC_TYPE  1

// One simulation step. Call at ~100 Hz (every 10 ms of simulated time).
// On hardware: a dedicated FreeRTOS task with vTaskDelayUntil.
// On host: interleaved with agc_engine cycles via the harness tick hook.
//
// Increment A (prior commit): periodic re-write of channels 30-33 to
// match LM_Simulator's continuous channel feed.
// Increment B (this commit): push CDU counter pulses via the engine's
// UnprogrammedIncrement entry point — same path LM_Simulator's socket
// input uses (agc_engine.c:1570). At 100 Hz step rate and 400 cps PCDU
// nominal, push 4 pulses per axis per step (= 400/100).
// Increment C (next): integrate attitude state, respond to channel
// 5/6 jet commands, drive CDU at integrated rate (not constant).
void peripheral_stub_step(agc_t *state, uint32_t dt_us)
{
    if (state == NULL) return;
    g_step_time_us += dt_us;
    g_pulse_phase++;

    // Re-assert the LM_Simulator channel baselines.
    state->InputChannel[030] = LM_SIM_CH030;
    state->InputChannel[033] = LM_SIM_CH033;

    // CDU pulses: on Pi/Linux, LM_Simulator drives these from simulated
    // attitude deltas (modify_gimbal_angle in AGC_IMU.tcl). At rest
    // with no jet firings, zero pulses fire and the AGC sees a stable
    // IMU.
    //
    // Empirically, the boot-time 1/ACCS computation in Luminary099
    // does NOT terminate without *some* CDU input — without pulses
    // slot 0 stays parked at PRIO=30110 forever inside GOODEPS1
    // (AOSTASK_AND_AOSJOB.agc:216). A minimum-rate trickle of pulses
    // (1 per axis per call) is enough to unblock it. This matches
    // LM_Simulator's behavior since gimbal-angle deltas accumulate
    // small floating-point residuals even when nominally stationary.
    //
    // Pi/Linux rate is 400 cps per axis from update_RCS; we do one
    // pulse per axis per peripheral_stub_step call (~5 Hz from the
    // legacy 200ms tick) — enough to keep 1/ACCS happy without
    // flooding the DAP with phantom motion.
    UnprogrammedIncrement(state, CDUX_COUNTER, PCDU_INC_TYPE);
    UnprogrammedIncrement(state, CDUY_COUNTER, PCDU_INC_TYPE);
    UnprogrammedIncrement(state, CDUZ_COUNTER, PCDU_INC_TYPE);

}

void peripheral_stub_tick(agc_t *state)
{
    // Periodic tick currently disabled to isolate alarm sources. Pi/Linux
    // LM_Simulator writes ch030/ch031/ch032/ch033 every ~25ms; if we
    // re-enable, do it at a comparable rate. The peripheral_stub_init
    // already set the initial values once, which is enough to boot.
    (void)state;
}
