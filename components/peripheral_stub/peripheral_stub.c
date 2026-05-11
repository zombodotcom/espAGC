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

// Boot-time channel values. Per ch030 being inverted-sense (0=signal
// present), starting with 037777 means "no signals yet" — IMU not
// operating, AGS in control, no PIPA freshness, temp out of limits.
// This is the upstream yaAGC default (agc_engine_init.c:255).
//
// We previously used 037377 (Apollo11-launch.canned time 0) which has
// bit 9 cleared = "IMU OPERATE WITH NO MALFUNCTION asserted". Tests
// confirmed that pre-asserting IMU at boot causes Luminary's
// interpretive code to enter an infinite GOTO loop at fixed-fixed
// Z=06647-06674 (V67WW INTSTALL/INTWAKE area) around cycle 100K — the
// real boot needs to see IMU come up VIA the documented turn-on
// sequence, not pre-asserted. The peripheral_stub_step then transitions
// channels to "IMU healthy" once Luminary's startup has progressed.
//
// LM_Simulator's set_ini_values uses 036331 — same problem, even more
// signals pre-asserted. The fresh-state upstream default is the right
// starting point; specific bits get cleared by the simulator as the
// boot sequence advances.
#define LM_SIM_CH030  037777   // upstream yaAGC default — no signals yet
#define LM_SIM_CH031  077777   // upstream yaAGC default
#define LM_SIM_CH032  077777   // upstream yaAGC default
#define LM_SIM_CH033  077777   // upstream yaAGC default

// Step accounting: tracks total simulated time and pulse-emission cadence.
// Reset by peripheral_stub_init.
static uint64_t g_step_time_us = 0;
static uint32_t g_pulse_phase  = 0;  // increments every step.

// --- LM attitude state (Increment C) ------------------------------------
// Modelled on lm_simulator.tcl's IMU state. All angles in milli-degrees
// (integer math, no FPU on ESP32 worth depending on). 360 deg = 360000.
//
// jet_quad tracks 16 RCS jet enable bits decoded from ch005/ch006 (see
// lm_simulator.tcl AGC_Outputs.tcl process_data for the exact bit map).
// nv/nu/np are net jet counts per axis (yaw/pitch/roll in LM body frame)
// derived from jet_quad via the same lookup LM_Simulator uses.
//
// On each step:
//   1. Decode latest jet_quad → net torque per body axis.
//   2. Integrate body rates by jet acceleration * dt.
//   3. Integrate stable-member angles by body rate * dt (skip the full
//      body-to-stable transform for now — small-angle approximation, the
//      AGC doesn't care about precise dynamics as long as ICDU pulses
//      arrive at sensible rates).
//   4. Compute delta between integrated angle and last-pushed angle.
//   5. Push PCDU/MCDU pulses to bring AGC's CDU counter up to the
//      integrated angle.
//
// The KEY DIFFERENCE from blindly streaming pulses: when the spacecraft
// is at rest with no jets firing, angles don't change, no pulses are
// pushed. Luminary's IMUMON (T4RUPT bank 6) doesn't see phantom CDU
// motion and doesn't loop on bit-change scans.
#define MDEG_PER_DEG       1000
#define MDEG_FULL_CIRCLE   (360 * MDEG_PER_DEG)
// One PCDU pulse advances the AGC's CDU counter by one increment. The
// AGC scales CDUX/Y/Z as PI radians / 2^14 — i.e. one increment is
// 180/16384 = 0.01099 degrees ≈ 11 milli-degrees. Round to 11 for the
// pulse threshold so we push roughly one pulse per 11 mdeg of motion.
#define MDEG_PER_CDU_PULSE 11

// Net jet acceleration in milli-deg per sec per sec, taken from
// dynamic_simulation in AGC_Simulation_Monitor_Control.tcl using the
// Apollo 11 ascent-stage LM_Weight default. The Tcl computes
// Alpha_Yaw = b + a/(m + c) in radians/sec/sec; for the LM ascent at
// ~4700 kg, that works out to ~13.5 deg/sec/sec per jet pair. Use a
// conservative 8 deg/sec/sec scaled to milli-deg.
#define JET_ACCEL_MDEG_PER_S2  8000

static int32_t g_att_x_mdeg = 0, g_att_y_mdeg = 0, g_att_z_mdeg = 0;
static int32_t g_pimu_x_mdeg = 0, g_pimu_y_mdeg = 0, g_pimu_z_mdeg = 0;
static int32_t g_rate_x_mdeg_s = 0, g_rate_y_mdeg_s = 0, g_rate_z_mdeg_s = 0;
static uint16_t g_ch005 = 0, g_ch006 = 0;
static int g_zero_imu = 0;

// Decode ch005/ch006 into net jet count per body axis. lm_simulator.tcl
// process_data lines 813-820 do the same thing.
static void decode_jets(int *nv, int *nu, int *np)
{
    // ch005 bits 1-8: Q4U Q4D Q3U Q3D Q2U Q2D Q1U Q1D
    int q4u = (g_ch005 >> 1) & 1, q4d = (g_ch005 >> 2) & 1;
    int q3u = (g_ch005 >> 3) & 1, q3d = (g_ch005 >> 4) & 1;
    int q2u = (g_ch005 >> 5) & 1, q2d = (g_ch005 >> 6) & 1;
    int q1u = (g_ch005 >> 7) & 1, q1d = (g_ch005 >> 8) & 1;
    // ch006 bits 1-8: Q3A Q4F Q1F Q2A Q2L Q3R Q4R Q1L
    int q3a = (g_ch006 >> 1) & 1, q4f = (g_ch006 >> 2) & 1;
    int q1f = (g_ch006 >> 3) & 1, q2a = (g_ch006 >> 4) & 1;
    int q2l = (g_ch006 >> 5) & 1, q3r = (g_ch006 >> 6) & 1;
    int q4r = (g_ch006 >> 7) & 1, q1l = (g_ch006 >> 8) & 1;
    // Net yaw  (V axis): +Q2D/+Q4U minus -Q2U/-Q4D
    *nv = (q2d + q4u) - (q2u + q4d);
    // Net pitch (U axis): +Q1D/+Q3U minus -Q1U/-Q3D
    *nu = (q1d + q3u) - (q1u + q3d);
    // Net roll (P axis): +(Q1F/Q2L/Q3A/Q4R) minus -(Q1L/Q2A/Q3R/Q4F)
    *np = (q1f + q2l + q3a + q4r) - (q1l + q2a + q3r + q4f);
}

static void push_cdu_delta(agc_t *state, int counter,
                           int32_t *att_mdeg, int32_t *pimu_mdeg)
{
    int32_t delta = *att_mdeg - *pimu_mdeg;
    // Wrap delta to [-180000, +180000] to handle 360-degree wraparound.
    if (delta < -MDEG_FULL_CIRCLE / 2) delta += MDEG_FULL_CIRCLE;
    if (delta >  MDEG_FULL_CIRCLE / 2) delta -= MDEG_FULL_CIRCLE;
    while (delta >= MDEG_PER_CDU_PULSE) {
        UnprogrammedIncrement(state, counter, 1);   // PCDU
        *pimu_mdeg += MDEG_PER_CDU_PULSE;
        if (*pimu_mdeg >= MDEG_FULL_CIRCLE) *pimu_mdeg -= MDEG_FULL_CIRCLE;
        delta -= MDEG_PER_CDU_PULSE;
    }
    while (delta <= -MDEG_PER_CDU_PULSE) {
        UnprogrammedIncrement(state, counter, 3);   // MCDU
        *pimu_mdeg -= MDEG_PER_CDU_PULSE;
        if (*pimu_mdeg < 0) *pimu_mdeg += MDEG_FULL_CIRCLE;
        delta += MDEG_PER_CDU_PULSE;
    }
}

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

    // No erasable-cell pre-seeding. FRESH_START_AND_RESTART.agc:430-450
    // explicitly initializes MASS, HIASCENT, DAP coefficients, RCSFLAGS,
    // and DAPBOOLS itself during cold boot — our previous attempts to
    // pre-seed these were either redundant (FRESH_START overwrote them)
    // or actively harmful (pre-asserting RCSFLAGS bit 13 / ACCSOKAY
    // before FRESH_START ran caused DAPIDLER's first T5RUPT to take a
    // different branch and ended up in the V67WW interpretive GOTO loop).
    //
    // Pad-load values that crews entered via V21NxxE (IMU compensation,
    // W-matrix scaling, etc.) are NOT initialized by FRESH_START — but
    // those cells aren't read during cold boot before V35E completes,
    // so they don't need seeding for the keypad-test scenario.

    g_step_time_us = 0;
    g_pulse_phase  = 0;
    g_att_x_mdeg = g_att_y_mdeg = g_att_z_mdeg = 0;
    g_pimu_x_mdeg = g_pimu_y_mdeg = g_pimu_z_mdeg = 0;
    g_rate_x_mdeg_s = g_rate_y_mdeg_s = g_rate_z_mdeg_s = 0;
    g_ch005 = g_ch006 = 0;
    g_zero_imu = 0;
}

void peripheral_stub_on_output(int channel, int value)
{
    if (channel == 005) {
        g_ch005 = (uint16_t)(value & 0xFFFF);
    } else if (channel == 006) {
        g_ch006 = (uint16_t)(value & 0xFFFF);
    } else if (channel == 012) {
        // Bit 5 of ch012 = ISS ZERO command. lm_simulator.tcl process_data
        // line 841 calls zeroIMU when this transitions to 1.
        if (value & 0x10) {
            g_att_x_mdeg = g_att_y_mdeg = g_att_z_mdeg = 0;
            g_pimu_x_mdeg = g_pimu_y_mdeg = g_pimu_z_mdeg = 0;
            g_rate_x_mdeg_s = g_rate_y_mdeg_s = g_rate_z_mdeg_s = 0;
            g_zero_imu = 1;
        } else {
            g_zero_imu = 0;
        }
    }
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

    // Continuous channel feed at lm_simulator.tcl's cadence (every step).
    // LM_Simulator writes all four of 30/31/32/33 — match.
    state->InputChannel[030] = LM_SIM_CH030;
    state->InputChannel[031] = LM_SIM_CH031;
    state->InputChannel[032] = LM_SIM_CH032;
    state->InputChannel[033] = LM_SIM_CH033;

    if (g_zero_imu) {
        // ISS ZERO active — AGC commanded the IMU to zero. Don't drive
        // any motion while it's zeroing.
        return;
    }

    // Decode current jet enable state into net torques per body axis.
    int nv, nu, np;
    decode_jets(&nv, &nu, &np);

    // Integrate angular acceleration into body rate. dt_us / 1e6 gives
    // seconds; we keep milli-deg/sec internally so multiply accel by
    // dt_ms / 1000 and add.
    int32_t dt_ms = (int32_t)(dt_us / 1000);
    if (dt_ms < 1) dt_ms = 1;
    g_rate_x_mdeg_s += (int32_t)nv * JET_ACCEL_MDEG_PER_S2 * dt_ms / 1000;
    g_rate_y_mdeg_s += (int32_t)nu * JET_ACCEL_MDEG_PER_S2 * dt_ms / 1000;
    g_rate_z_mdeg_s += (int32_t)np * JET_ACCEL_MDEG_PER_S2 * dt_ms / 1000;

    // Integrate rate into attitude angle.
    int32_t da_x = g_rate_x_mdeg_s * dt_ms / 1000;
    int32_t da_y = g_rate_y_mdeg_s * dt_ms / 1000;
    int32_t da_z = g_rate_z_mdeg_s * dt_ms / 1000;
    g_att_x_mdeg += da_x;
    g_att_y_mdeg += da_y;
    g_att_z_mdeg += da_z;
    while (g_att_x_mdeg <  0)               g_att_x_mdeg += MDEG_FULL_CIRCLE;
    while (g_att_x_mdeg >= MDEG_FULL_CIRCLE) g_att_x_mdeg -= MDEG_FULL_CIRCLE;
    while (g_att_y_mdeg <  0)               g_att_y_mdeg += MDEG_FULL_CIRCLE;
    while (g_att_y_mdeg >= MDEG_FULL_CIRCLE) g_att_y_mdeg -= MDEG_FULL_CIRCLE;
    while (g_att_z_mdeg <  0)               g_att_z_mdeg += MDEG_FULL_CIRCLE;
    while (g_att_z_mdeg >= MDEG_FULL_CIRCLE) g_att_z_mdeg -= MDEG_FULL_CIRCLE;

    // Push CDU pulses to match the integrated angle.
    push_cdu_delta(state, CDUX_COUNTER, &g_att_x_mdeg, &g_pimu_x_mdeg);
    push_cdu_delta(state, CDUY_COUNTER, &g_att_y_mdeg, &g_pimu_y_mdeg);
    push_cdu_delta(state, CDUZ_COUNTER, &g_att_z_mdeg, &g_pimu_z_mdeg);
}

// Stuck-job recovery via simulated GOJAM. Cold-boot Luminary's
// 1/ACCSET (PRIO27 + offset 110 = priority 027110, allocated by
// DAPIDLER) enters interpretive code that gets caught in
// INTERPRETER.agc:681 GOTO indirection with POLISH=0 reading
// Erasable[4][044]=0 indefinitely. Block II AGC is non-preemptive
// and the GOTOERS loop doesn't reach DANZIG (where NEWJOB is
// checked), so the executive can never swap CHARIN in.
//
// Real Apollo had this never happen because the crew's pre-launch
// PAD LOAD set MASS/DAP coefficients to values that let 1/ACCS
// converge cleanly. Without those values, 1/ACCS's interpretive
// computation degenerates. LM_Simulator over a socket would also
// keep the engine fed via ParseIoPacket / UnprogrammedIncrement.
//
// Recovery: when NEWJOB indicates a higher-priority job is waiting
// across multiple consecutive ticks (current job never yields),
// simulate a hardware GOJAM — the same recovery yaAGC does on alarm
// trips (agc_engine.c:2246-2295). GOJAM sets Z=04000, clears
// interrupt state, and runs FRESH_START → DORSTART, which restarts
// the system from a known-clean state. The pending CHARIN job in
// slot 1 is restored by DORSTART's RESTART logic (slot CADRs are
// preserved across software restarts), so the keypress that
// triggered the original CHARIN allocation gets processed normally.
// Rescue can fire multiple times. Each GOJAM gets the engine past
// one round of 1/ACCSET deadlock, but since FRESH_START re-clears
// RCSFLAGS bit 13, DAPIDLER will re-NOVAC 1/ACCSET on its next
// T5RUPT. The rescue fires again on the next stuck detection. Cap at
// MAX_RESCUES to avoid runaway GOJAM if something else is broken.
#define STUCK_THRESHOLD   2
// One-shot rescue at boot: get past the initial 1/ACCSET deadlock.
// After that, normal Luminary dispatch handles things — additional
// rescues risk killing legitimate verb-execution flows that hold
// NEWJOB while waiting for user input (V37 REQMM, etc.).
#define MAX_RESCUES       1
static int      g_last_newjob   = 0;
static int      g_stuck_count   = 0;
static int      g_rescue_count  = 0;

static void simulate_gojam(agc_t *s)
{
    // Mirror agc_engine.c GOJAM (line 2246-2298) exactly.
    s->ExtraDelay += 2;                      // GOJAM + TC 4000 timing
    s->Erasable[0][2] = s->Erasable[0][5];   // RegQ <- old Z
    s->Erasable[0][5] = 04000;               // RegZ -> FRESH_START
    s->InIsr = 0;
    s->AllowInterrupt = 1;
    s->ParityFail = 0;
    s->Trap31A = s->Trap31B = s->Trap32 = 0;
    for (int i = 1; i <= NUM_INTERRUPT_TYPES; i++) s->InterruptRequests[i] = 0;
    s->InputChannel[005] = 0;
    s->InputChannel[006] = 0;
    s->InputChannel[010] = 0;
    s->InputChannel[011] = 0;
    s->InputChannel[012] = 0;
    s->InputChannel[013] = 0;
    s->InputChannel[014] = 0;
    s->InputChannel[033] |= 002000;          // UPLINK TOO FAST
    s->InputChannel[034] = 0;
    s->InputChannel[035] = 0;
    s->DownruptTimeValid = 0;
    s->IndexValue = 0;
    s->ExtraCode = 0;
    s->SubstituteInstruction = 0;
    s->PendFlag = 0;
    s->PendDelay = 0;
    s->TookBZF = 0;
    s->TookBZMF = 0;
    s->RestartLight = 1;
    s->GeneratedWarning = 1;
}

static void rescue_stuck_job(agc_t *state)
{
    if (g_rescue_count >= MAX_RESCUES) return;
    int newjob = state->Erasable[0][067] & 077777;
    int swap_pending = (newjob != 0 && newjob != 077777);

    if (swap_pending && newjob == g_last_newjob) {
        g_stuck_count++;
        if (g_stuck_count >= STUCK_THRESHOLD) {
            simulate_gojam(state);
            g_stuck_count = 0;
            g_rescue_count++;
        }
    } else {
        g_stuck_count = 0;
    }
    g_last_newjob = newjob;
}

// Look for an unhandled CHARIN slot (PRIO=30110, LOC=02077,
// BANKSET=60101). If found, an external keypress allocated CHARIN
// but the executive can't dispatch it (1/ACCSET blocking, or a
// stale priority-30 job from an earlier keypress is "active"). Free
// any blocking active slot, then set up the engine to execute
// CHARIN's first instruction directly.
//
// CHARIN entry per PINBALL_GAME__BUTTONS_AND_LIGHTS.agc:475:
//   bank 040 (FBANK=030+SBANK), offset 02077, EBANK=1
// In yaAGC: Z=02077 with FBANK=030 and ch7 bit 6 set selects bank 040.
#define CHARIN_LOC      02077
#define CHARIN_BANKSET  060101  /* FBANK=030, SBANK in bit 6, EBANK=1 */
#define CHARIN_PRIORITY 030110

// 1/ACCSET's signature: PRIO = 027110 (PRIO27 + work area offset 0110).
// This is the specific deadlock we're rescuing. Don't intervene for any
// other active job — let Luminary's normal dispatch handle them.
#define STUCK_1_ACCSET_PRIO 027110

static void dispatch_pending_charin(agc_t *s)
{
    if (s->InIsr) return;       // don't disturb interrupt processing

    // Only intervene when the ACTIVE slot is the known-bad 1/ACCSET
    // (PRIO=027110). Other legitimate running jobs must not be
    // disturbed — that breaks normal verb-execution flow (e.g., V37
    // sleeps in INTSTALL waiting for the user to type the program
    // number; if we kill its slot, the program-change never completes).
    int active_prio = s->Erasable[0][0167] & 077777;
    if (active_prio != STUCK_1_ACCSET_PRIO) return;

    // Look for a CHARIN slot (skip slot 0, which is the active set).
    int charin_slot = -1;
    for (int slot = 1; slot < 8; slot++) {
        int base = 0154 + slot * 014;
        int prio = s->Erasable[0][base + 11] & 077777;
        int loc  = s->Erasable[0][base + 8]  & 077777;
        int bset = s->Erasable[0][base + 9]  & 077777;
        if (prio == CHARIN_PRIORITY && loc == CHARIN_LOC &&
            bset == CHARIN_BANKSET) {
            charin_slot = slot;
            break;
        }
    }
    if (charin_slot < 0) return;

    // Free the stuck 1/ACCSET active slot.
    s->Erasable[0][0167] = 077777;

    // Manual CHANG2 swap: copy slot N's state to active.
    int src = 0154 + charin_slot * 014;
    for (int off = 0; off < 014; off++) {
        s->Erasable[0][0154 + off] = s->Erasable[0][src + off];
    }
    // Free the source slot.
    for (int off = 0; off < 014; off++) {
        s->Erasable[0][src + off] = 0;
    }
    s->Erasable[0][src + 11] = 077777;

    // Set engine state to start executing CHARIN's first instruction.
    s->Erasable[0][5] = CHARIN_LOC;           // RegZ
    s->Erasable[0][4] = 030 << 10;            // RegFB = FBANK 030
    s->Erasable[0][6] = CHARIN_BANKSET;       // RegBB
    s->Erasable[0][3] = 1;                    // RegEB = 1
    s->OutputChannel7 |= 0100;                // superbank bit
    s->Erasable[0][067] = 077777;             // NEWJOB cleared
}

void peripheral_stub_tick(agc_t *state)
{
    if (state == NULL) return;
    // channel_router_on_routine calls us every ~16k engine cycles
    // (~200ms simulated time). Drive one simulator step per tick.
    peripheral_stub_step(state, 200000);

    rescue_stuck_job(state);
    dispatch_pending_charin(state);

    // Continuously assert RCSFLAGS bit 13. DAPIDLER's T5RUPT checks
    // this bit and only NOVACs 1/ACCSET if it's CLEAR. FRESH_START
    // explicitly clears it (FRESH_START_AND_RESTART.agc:430), but
    // we keep re-setting it from outside so DAPIDLER takes the
    // CHECKUP branch instead of allocating 1/ACCSET — which is the
    // job that gets stuck in V67WW INTWAKE loop. Only re-assert AFTER
    // at least one rescue has fired, so we don't perturb the very
    // first FRESH_START's setup.
    // RCSFLAGS at erasable 01273 = Erasable[2][0273]. BIT13 = 04000.
    if (g_rescue_count > 0) {
        state->Erasable[2][0273] |= 04000;
    }
}
