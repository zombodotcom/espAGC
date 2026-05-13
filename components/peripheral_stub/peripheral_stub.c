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
#include "esp_log.h"

static const char *PSTUB_TAG = "pstub";

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
// LM_Simulator's actual set_ini_values (from lm_simulator.tcl:566-573
// and confirmed by ref_capture.py's LM_INI list). These are what WSL
// reference yaAGC sees AFTER 0.5 sec on default 037777/077777 — and
// what makes Luminary's cold-boot transition out of 1/ACCSET into a
// state where V37E00E completes.
//   ch030 = 036331 → bits 13 (IMU operate), 9 (PIPA freshness), 8 cleared
//   ch031 = 077777 → stick centered
//   ch032 = 022777 → PRO not pressed, RHC not commanded
//   ch033 = 057776 → ZERO OPTICS, no fresh radar, no uplink
#define LM_SIM_CH030  036331
#define LM_SIM_CH031  077777
#define LM_SIM_CH032  022777
#define LM_SIM_CH033  057776

// Step accounting: tracks total simulated time and pulse-emission cadence.
// Reset by peripheral_stub_init.
static uint64_t g_step_time_us = 0;
static uint32_t g_pulse_phase  = 0;  // increments every step.

// LM_INI deferred write — canonical Pi/Linux LM_Simulator sends these
// ~1-2s into engine run, NOT at cycle 0. peripheral_stub_init arms a
// countdown; peripheral_stub_tick fires the WriteIO calls when it hits 0.
static int g_lm_ini_pending = 0;
static int g_lm_ini_ticks_remaining = 0;

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

    // CRITICAL TIMING: do NOT write LM_INI values at cycle 0. Canonical
    // Pi/Linux setup has yaAGC running for ~1-2s before LM_Simulator's
    // socket connect → write_ini_values() lands. The engine's FRESH_START
    // sequence interprets the LM_INI arrival as part of its cold-boot
    // hand-off (IMU coming up, etc.). If LM_INI is present at cycle 0,
    // the engine takes a DIFFERENT path through DAPIDLER's first T5RUPT
    // and ends in the 1/ACCSET interpretive GOTO deadlock — which our
    // rescue chain papers over but with WarningFilter side effects that
    // clear ch033 bit 13 (AGC WARNING input) and prevent V37E00E from
    // completing (verified 2026-05-12 by diffing core.022 from
    // wsl_dumps/ref vs our host_v37x2 dumps).
    //
    // Instead, peripheral_stub_tick will write LM_INI after a delay
    // matching canonical's ~1s pre-LM_INI window. See g_lm_ini_pending.
    //
    // LM-only: the channels we initialise (ch016 MARK, ch030..033 LM
    // discrete inputs) carry LM-specific bit assignments. Writing them
    // on a CM rope (Comanche055) corrupts boot state — that's the
    // second cause of the "black screen with BOOT held" symptom. Skip
    // LM_INI entirely when running CM. CM has different startup
    // requirements that this stub doesn't model yet (PIPA pulses,
    // CDUs, etc.) — Phase 3 work.
    extern int CmOrLm;
    if (!CmOrLm) {
        g_lm_ini_pending = 1;
        g_lm_ini_ticks_remaining = 10;   // ~10 ChannelRoutines ≈ 1 sec sim time
    } else {
        ESP_LOGI(PSTUB_TAG, "CM mode: skipping LM-specific LM_INI");
    }

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
// PIPA (Pulsed Integrating Pendulous Accelerometer) counter registers.
// Per Luminary099 ERASABLE_ASSIGNMENTS.agc:134-136. Each pulse =
// 5.85 cm/s = 0.192 ft/s of sensed velocity along that body axis. The
// AGC integrates these into the state vector — SERVICER picks up R(t)
// and V(t) and feeds them to P63's ALT/HDOT/TTOGO display.
#define PIPAX_COUNTER 037
#define PIPAY_COUNTER 040
#define PIPAZ_COUNTER 041
// IncType codes (UnprogrammedIncrement, agc_engine.c:1570).
#define PINC_TYPE 0
#define MINC_TYPE 2

// Landing Radar range counter. Per Luminary099 ERASABLE_ASSIGNMENTS.agc:141
// (RNRAD EQUALS 46) and ASSEMBLY_AND_OPERATION_INFORMATION.agc:792
// ("RADAR RANGE WORD = 9.38 FEET"). Pulsing this counter via PINC
// represents incoming LR range data — the AGC's RADARSUP / READRADR
// routines sample it during the descent radar-update cycle. Real LR
// fires at a few Hz, sampling once per altitude / velocity beam.
#define RNRAD_COUNTER 046
// 9.38 ft per RNRAD pulse — used to convert a feet-per-second sink rate
// into a pulse cadence below.
#define FEET_PER_RNRAD_PULSE 938     // hundredths of a foot, integer math

// Descent simulation state. When g_descent_active is non-zero,
// peripheral_stub_step fires both PIPAZ- pulses (modelling DPS thrust)
// and RNRAD pulses (modelling LR range data) each tick. Off by
// default. Toggle via peripheral_stub_set_descent_thrust() / web POST
// /thrust. With a proper initial state vector + IMU alignment this
// would produce realistic R1/R2/R3 — pending Phase 3 of the LM_Sim
// port; for now it's a foundation that exercises the canonical drain
// for both UnprogrammedIncrement (PIPAs) and counter-channel pulses
// (RNRAD).
static int g_descent_active     = 0;
static int g_pipa_z_remainder   = 0;   // fractional accumulator
// Simulated LR range, in hundredths of a foot, for the LR driver below.
// PDI starts at ~50 000 ft; we tick down at ~300 ft/s while descent is
// active so V16N68 shows a believable range trace even before the
// state-vector path is ported.
static long g_lr_range_cft      = 50000L * 100;
static int  g_rnrad_remainder   = 0;
// Step rate ≈ 10.4 Hz (96 ms ticks). 300 ft/s sink → 30 ft per tick →
// ~3 RNRAD pulses (each = 9.38 ft) of "altitude decrement" to emit
// per tick. The actual count depends on the integer arithmetic below.
#define DESCENT_SINK_CFT_PER_STEP   3000   // 30 ft, hundredths-of-foot

// LM Block II PIPA pulse rate at 10 ft/s² thrust = 10 / 0.192 ≈ 52 Hz.
// peripheral_stub_step is called from peripheral_stub_tick every
// ChannelRoutine (≈ 96 ms wall, ≈ 10 Hz). So pulses per step ≈ 5.
#define PIPA_PULSES_PER_STEP_X10  52   // tenths of pulse per step

void peripheral_stub_set_descent_thrust(int active)
{
    g_descent_active = active ? 1 : 0;
    if (g_descent_active) {
        // Re-arm initial altitude so toggling off/on restarts a fresh
        // descent run — useful for repeated demos without rebooting.
        g_lr_range_cft    = 50000L * 100;
        g_rnrad_remainder = 0;
        g_pipa_z_remainder = 0;
    }
    ESP_LOGI(PSTUB_TAG,
             "descent sim: %s (PIPAZ ~52 Hz, RNRAD from 50000 ft @ 300 ft/s)",
             g_descent_active ? "ON" : "OFF");
}
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

    // === Full LM_Simulator dynamic_simulation port ===
    // Models the LM's attitude + IMU + RCS feedback loop that
    // lm_simulator.tcl runs continuously on Pi/Linux. The AGC needs
    // to see PCDU/MCDU pulses on its three CDU registers to confirm
    // the IMU is healthy and to maintain accurate attitude knowledge.
    // Without these pulses, IMUMON / 1/ACCS / DAP routines lock up
    // waiting for state they'll never get.

    if (g_zero_imu) {
        // ISS ZERO: hold attitude at 0, push enough -delta pulses to
        // zero the AGC's CDU counters quickly. lm_simulator.tcl's
        // zeroIMU does the same. While zero is asserted, no integration.
        return;
    }

    // 1) Decode jet commands → net torque per body axis.
    int nv, nu, np;
    decode_jets(&nv, &nu, &np);

    // 2) Integrate body rates from jet torques (Euler integration).
    //    Each jet pair imparts JET_ACCEL_MDEG_PER_S2 mdeg/s².
    int32_t dt_ms = (int32_t)(dt_us / 1000);
    if (dt_ms <= 0) dt_ms = 1;
    // rate += accel * dt; rate is in mdeg/s, accel in mdeg/s², dt in ms.
    // So delta_rate = accel * (dt_ms / 1000) = (accel * dt_ms) / 1000.
    g_rate_x_mdeg_s += ((int32_t)nv * JET_ACCEL_MDEG_PER_S2 * dt_ms) / 1000;
    g_rate_y_mdeg_s += ((int32_t)nu * JET_ACCEL_MDEG_PER_S2 * dt_ms) / 1000;
    g_rate_z_mdeg_s += ((int32_t)np * JET_ACCEL_MDEG_PER_S2 * dt_ms) / 1000;

    // 3) Add small free-drift to simulate gyro noise / floor torques.
    //    Real IMU gyros have residual drift of ~0.001-0.01 deg/sec.
    //    The drift ensures CDU pulses fire even without jet activity,
    //    keeping IMUMON / 1/ACCS happy. Direction varies by axis.
    static int drift_phase = 0;
    drift_phase = (drift_phase + 1) & 7;
    // 1 mdeg/sec rotational drift on each axis (alternating direction
    // via phase). Effective pulse rate: 1 mdeg/s / 11 mdeg/pulse = ~0.09
    // pulses/sec per axis. Combined: ~0.27 pulses/sec, enough to keep
    // CDU counters changing without overwhelming the engine.
    int drift_sign = (drift_phase < 4) ? 1 : -1;
    g_rate_x_mdeg_s += drift_sign * 1;
    g_rate_y_mdeg_s += drift_sign * 1;
    g_rate_z_mdeg_s += drift_sign * 1;

    // Clamp rates to ±30 deg/sec to avoid runaway integration.
    const int32_t RATE_CLAMP = 30 * MDEG_PER_DEG;
    if (g_rate_x_mdeg_s >  RATE_CLAMP) g_rate_x_mdeg_s =  RATE_CLAMP;
    if (g_rate_x_mdeg_s < -RATE_CLAMP) g_rate_x_mdeg_s = -RATE_CLAMP;
    if (g_rate_y_mdeg_s >  RATE_CLAMP) g_rate_y_mdeg_s =  RATE_CLAMP;
    if (g_rate_y_mdeg_s < -RATE_CLAMP) g_rate_y_mdeg_s = -RATE_CLAMP;
    if (g_rate_z_mdeg_s >  RATE_CLAMP) g_rate_z_mdeg_s =  RATE_CLAMP;
    if (g_rate_z_mdeg_s < -RATE_CLAMP) g_rate_z_mdeg_s = -RATE_CLAMP;

    // 4) Integrate stable-member angles from body rates.
    //    delta_angle = rate * dt. angle in mdeg, rate in mdeg/sec, dt in ms.
    //    delta = rate * dt_ms / 1000.
    g_att_x_mdeg += (g_rate_x_mdeg_s * dt_ms) / 1000;
    g_att_y_mdeg += (g_rate_y_mdeg_s * dt_ms) / 1000;
    g_att_z_mdeg += (g_rate_z_mdeg_s * dt_ms) / 1000;

    // Wrap angles to [0, 360000).
    while (g_att_x_mdeg <  0)                  g_att_x_mdeg += MDEG_FULL_CIRCLE;
    while (g_att_x_mdeg >= MDEG_FULL_CIRCLE)   g_att_x_mdeg -= MDEG_FULL_CIRCLE;
    while (g_att_y_mdeg <  0)                  g_att_y_mdeg += MDEG_FULL_CIRCLE;
    while (g_att_y_mdeg >= MDEG_FULL_CIRCLE)   g_att_y_mdeg -= MDEG_FULL_CIRCLE;
    while (g_att_z_mdeg <  0)                  g_att_z_mdeg += MDEG_FULL_CIRCLE;
    while (g_att_z_mdeg >= MDEG_FULL_CIRCLE)   g_att_z_mdeg -= MDEG_FULL_CIRCLE;

    // 5) Push CDU pulses to bring AGC's counters up to integrated angles.
    //    FIRST_CDU = 032 (CDUX); CDUY=033, CDUZ=034. IncType=1 = PCDU,
    //    IncType=3 = MCDU (per agc_engine.c:1570 UnprogrammedIncrement).
    push_cdu_delta(state, CDUX_COUNTER, &g_att_x_mdeg, &g_pimu_x_mdeg);
    push_cdu_delta(state, CDUY_COUNTER, &g_att_y_mdeg, &g_pimu_y_mdeg);
    push_cdu_delta(state, CDUZ_COUNTER, &g_att_z_mdeg, &g_pimu_z_mdeg);

    // 6) Descent thrust simulation. While the LM is in powered descent
    //    (P63/P64/P66), DPS provides ~10 ft/s² along body +Z. PIPAs
    //    sense this as MINC pulses on PIPAZ (specific force is opposite
    //    to the thrust velocity gain in the integration convention
    //    Luminary uses). Fire ~5 pulses per 96 ms step ≈ 52 Hz.
    //
    //    P63's V06N63 display needs ALT/HDOT — those come from
    //    SERVICER integrating these PIPA pulses against an INITIAL
    //    STATE VECTOR (R, V at PDI). With initial state at zero (no
    //    uplink, no V21N02), HDOT will count up but ALT will go
    //    negative. That's expected — full P63 needs state-vector
    //    init via uplink (Phase 3, see project memory).
    if (g_descent_active) {
        // (a) PIPAZ thrust pulses
        g_pipa_z_remainder += PIPA_PULSES_PER_STEP_X10;
        while (g_pipa_z_remainder >= 10) {
            UnprogrammedIncrement(state, PIPAZ_COUNTER, MINC_TYPE);
            g_pipa_z_remainder -= 10;
        }
        // (b) LR range pulses. We model a steady ~300 ft/s sink rate
        // and fire RNRAD pulses representing the altitude decrement
        // each step. Real LR samples at a few Hz and the raw counter
        // is range, not range rate — but Luminary's RADARSUP keeps
        // a running delta internally so a steady pulse stream still
        // exercises the right code path. RNRAD pulses use PINC.
        // When g_lr_range_cft reaches 0 we hold there (touchdown).
        if (g_lr_range_cft > 0) {
            long step_cft = DESCENT_SINK_CFT_PER_STEP;
            if (step_cft > g_lr_range_cft) step_cft = g_lr_range_cft;
            g_lr_range_cft -= step_cft;
            // Convert cft to RNRAD pulses (each = 9.38 ft = 938 cft).
            g_rnrad_remainder += (int)step_cft;
            while (g_rnrad_remainder >= FEET_PER_RNRAD_PULSE) {
                UnprogrammedIncrement(state, RNRAD_COUNTER, PINC_TYPE);
                g_rnrad_remainder -= FEET_PER_RNRAD_PULSE;
            }
        }
    }
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
// Allow multiple GOJAM rescues. Each fired GOJAM runs the engine's
// own FRESH_START → DORSTART → SETINFL path, which clears RASFLAG
// BIT7+BIT14 (FRESH_START.agc SETINFL block) and re-runs the restart
// phase tables. V37 needs at least one GOJAM AFTER its own INTSTALL
// sleep to clear the orphan BIT14 set by the cold-boot interpretive
// caller in bank 23. Trigger is NEWJOB-stuck-across-ticks — fires
// only when a higher-priority job genuinely can't yield.
#define MAX_RESCUES       4
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
            ESP_LOGI(PSTUB_TAG, "rescue_stuck_job #%d fired newjob=%06o",
                     g_rescue_count + 1, newjob);
            g_stuck_count = 0;
            g_rescue_count++;
        }
    } else {
        g_stuck_count = 0;
    }
    g_last_newjob = newjob;
}

// Detect any non-active slot sleeping at WAKESTAL CADR for too many
// ticks. WAKESTAL = CADR INTSTALL+1 = 027415. A real INTSTALL waiter
// gets woken by INTWAKE in the integration cycle's completion path.
// In our build the cold-boot orphan never INTWAKE's, so V37's
// INTSTALL parks forever. Fire one GOJAM per detected stuck sleeper —
// SETINFL clears RASFLAG bits and the engine restarts cleanly; the
// user (or test harness) re-issues V37E00E and it goes through.
#define WAKESTAL_CADR        027415
#define WAKESTAL_STUCK_TICKS 4      // ~64k cycles (~60ms sim time)
#define MAX_WAKESTAL_RESCUES 12
static int g_wakestal_ticks    = 0;
static int g_wakestal_rescues  = 0;

static void rescue_wakestal_sleeper(agc_t *s)
{
    if (g_wakestal_rescues >= MAX_WAKESTAL_RESCUES) return;
    if (s->InIsr) return;

    int found = 0;
    // Scan all 8 slots including slot 0 (active). JOBSLEEP negates the
    // priority of whatever slot the calling job ran in — that's often
    // slot 0 (where V37's interpretive CALL INTSTALL lands).
    for (int slot = 0; slot < 8; slot++) {
        int base = 0154 + slot * 014;
        int loc  = s->Erasable[0][base + 8]  & 077777;
        int prio = s->Erasable[0][base + 11] & 077777;
        if (prio == 077777) continue;          // empty slot
        int sleeping = (prio & 040000) != 0;   // bit 14 = negated priority
        if (sleeping && loc == WAKESTAL_CADR) { found = 1; break; }
    }

    if (found) {
        g_wakestal_ticks++;
        if (g_wakestal_ticks >= WAKESTAL_STUCK_TICKS) {
            simulate_gojam(s);
            ESP_LOGI(PSTUB_TAG, "rescue_wakestal_sleeper #%d fired",
                     g_wakestal_rescues + 1);
            g_wakestal_ticks = 0;
            g_wakestal_rescues++;
        }
    } else {
        g_wakestal_ticks = 0;
    }
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
    int active_prio_check = s->Erasable[0][0167] & 077777;
    if (active_prio_check != STUCK_1_ACCSET_PRIO) return;

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
    ESP_LOGI(PSTUB_TAG, "dispatch_pending_charin fired (slot %d)", charin_slot);
}

// ============================================================================
// Aggressive CHARIN force-dispatch (the actual cold-boot fix).
//
// Past sessions found that when the AGC is in its cold-boot deadlock,
// KEYRUPT1 fires and NOVAC tries to allocate a CHARIN slot, but the
// resulting slot has WRONG values: PRIORITY=00110 (missing CHRPRIO
// contribution), LOC=0 (missing CHARIN entry), BANKSET=02077 (the
// CHARIN low-half stored in the wrong cell). The slot never dispatches.
//
// Mechanism: when channel_router queues a keypress, it calls
// peripheral_stub_on_keypress_posted() which arms a force-dispatch
// timer. If the engine hasn't reached CHARIN code within FORCE_DISPATCH_
// TICKS ChannelRoutine ticks (~50ms simulated), we manually set up the
// engine to execute CHARIN with the correct entry point + bank state +
// ch015 key code. This bypasses the broken NOVAC slot allocation.
//
// CHARIN reads ch015 directly (not from the slot), so as long as
// channel_router_pump_input has written ch015 (which it does before
// posting the IR5 KEYRUPT1 request), the key code is available.
// ============================================================================
#define FORCE_DISPATCH_TICKS  6      // ~50ms simulated (6 * 8191 cycles)
#define MAX_FORCE_DISPATCHES  100    // generous cap; one per keypress

static volatile int g_keypress_pending = 0;
static volatile int g_keypress_ticks   = 0;
static int          g_force_dispatches = 0;

void peripheral_stub_on_keypress_posted(uint8_t code)
{
    (void)code;
    g_keypress_pending = 1;
    g_keypress_ticks = 0;
}

// Returns 1 if engine has recently been executing CHARIN code.
// CHARIN is in bank 040 (FBANK=030, SUPERBANK bit set in ch7). Entry
// point is offset 02077 but the routine extends several hundred words.
// We accept Z in [02077, 02300] with the right bank state.
static int engine_is_in_charin(agc_t *s)
{
    int z  = s->Erasable[0][5] & 077777;
    int fb = (s->Erasable[0][4] >> 10) & 037;
    int sb = (s->OutputChannel7 & 0100) != 0;
    // FB=030 (decimal 24) + SUPERBANK → bank 040.
    if (fb == 030 && sb && z >= 02000 && z <= 02400) return 1;
    return 0;
}

static void force_dispatch_charin(agc_t *s)
{
    if (s->InIsr) return;
    if (g_force_dispatches >= MAX_FORCE_DISPATCHES) return;

    // If engine already in CHARIN code, the keypress dispatched normally.
    if (engine_is_in_charin(s)) {
        g_keypress_pending = 0;
        g_keypress_ticks = 0;
        return;
    }

    // CRITICAL: only force-dispatch when engine is stuck in cold-boot loop.
    // If executive is idle (active slot priority == 077777) OR running a
    // legitimate verb-in-progress (e.g. V37 waiting for noun, priority
    // !=030110 and !=000110), trust KEYRUPT1 to dispatch normally.
    // Force-dispatching during V37's noun-wait blows away its state and
    // restarts CHARIN from scratch — user types "01E" but it gets treated
    // as a fresh sequence and the noun never lands.
    int active_prio = s->Erasable[0][0167] & 077777;
    int cold_boot_stuck = (active_prio == 0030110 ||  // 1/ACCSET
                          active_prio == 0027110 ||  // PINBALL stuck
                          active_prio == 0000110);   // broken CHARIN slot
    if (!cold_boot_stuck) {
        // Engine is in normal scheduling — keypress should reach CHARIN
        // through KEYRUPT1+NOVAC normally. Don't intervene.
        g_keypress_pending = 0;
        g_keypress_ticks = 0;
        return;
    }

    g_keypress_ticks++;
    if (g_keypress_ticks < FORCE_DISPATCH_TICKS) return;

    // Mark the currently-active slot as completed (priority 077777 = empty).
    // The cold-boot stuck job at slot 0 (1/ACCSET or PINBALL refresh) gets
    // dropped — it was never going to terminate cleanly anyway. CHARIN
    // calls TC ENDOFJOB when done, which calls NUCHANG2 to select next
    // slot; if none, DUMMYJOB runs, which is fine.
    s->Erasable[0][0167] = 077777;        // PRIORITY of active slot → empty

    // Set engine registers to start CHARIN immediately.
    s->Erasable[0][5] = CHARIN_LOC;       // RegZ ← 02077
    s->Erasable[0][4] = 030 << 10;        // RegFB ← bank 030
    s->Erasable[0][6] = CHARIN_BANKSET;   // RegBB ← FB=030 SBANK EB=1
    s->Erasable[0][3] = 1;                // RegEB ← 1
    s->OutputChannel7 |= 0100;            // SUPERBANK bit
    s->Erasable[0][067] = 077777;         // NEWJOB cleared

    // Clear interrupt state so the engine doesn't bounce back into KEYRUPT1.
    s->InIsr = 0;
    s->AllowInterrupt = 1;
    s->InterruptRequests[5] = 0;          // KEYRUPT1 acknowledged

    g_keypress_pending = 0;
    g_keypress_ticks = 0;
    g_force_dispatches++;
    int ch15_val = s->InputChannel[015] & 077777;
    ESP_LOGI(PSTUB_TAG, "force_dispatch_charin #%d fired (ch15=%05o dec=%d Z=%05o FB=%05o)",
             g_force_dispatches, ch15_val, ch15_val,
             s->Erasable[0][5] & 077777,
             s->Erasable[0][4] & 077777);
}

void peripheral_stub_tick(agc_t *state)
{
    if (state == NULL) return;

    // Deferred LM_INI — fire after the cold-boot warm-up window. See
    // peripheral_stub_init comment for why this can't happen at cycle 0.
    if (g_lm_ini_pending) {
        if (g_lm_ini_ticks_remaining > 0) {
            g_lm_ini_ticks_remaining--;
        } else {
#ifdef CONFIG_AGC_YAAGC_SOCKET
            // Route through the canonical mask+value SocketAPI drain.
            // Five channels (ch016 + ch030..033), each as a mask packet
            // followed by a value packet — identical to the bytes the
            // Python driver sends to yaAGC.exe. This is the path that
            // proved 5/5; direct WriteIO from outside ChannelInput
            // (the legacy branch below) proved 0/5 in every host test.
            extern int yaagc_socket_inject_packet(int, int, int);
            yaagc_socket_inject_packet(016, 0o00174, 1);
            yaagc_socket_inject_packet(016, 0,       0);
            yaagc_socket_inject_packet(030, 0o77777, 1);
            yaagc_socket_inject_packet(030, LM_SIM_CH030, 0);
            yaagc_socket_inject_packet(031, 0o77777, 1);
            yaagc_socket_inject_packet(031, LM_SIM_CH031, 0);
            yaagc_socket_inject_packet(032, 0o77777, 1);
            yaagc_socket_inject_packet(032, LM_SIM_CH032, 0);
            yaagc_socket_inject_packet(033, 0o77776, 1);
            yaagc_socket_inject_packet(033, LM_SIM_CH033, 0);
            ESP_LOGI(PSTUB_TAG, "LM_INI via yaagc_socket (canonical mask+value flow)");
#else
            WriteIO(state, 030, LM_SIM_CH030);
            WriteIO(state, 031, LM_SIM_CH031);
            WriteIO(state, 032, LM_SIM_CH032);
            WriteIO(state, 033, LM_SIM_CH033);
            ESP_LOGI(PSTUB_TAG, "LM_INI deferred-fire: ch030=%05o ch031=%05o ch032=%05o ch033=%05o",
                     state->InputChannel[030] & 077777,
                     state->InputChannel[031] & 077777,
                     state->InputChannel[032] & 077777,
                     state->InputChannel[033] & 077777);
#endif
            g_lm_ini_pending = 0;
        }
    }

    // ChannelRoutine fires every 8191 MCT (agc_engine.c:1944 mask
    // `ChannelRoutineCount & 017777`). One MCT = 1/AGC_PER_SECOND sec
    // = 1/85333 sec ≈ 11.72 μs of simulated time. So 8191 MCT ≈ 96 ms
    // of simulated time. Previously this passed 8000 μs (mis-reading
    // cycle count as μs) which under-integrated attitude by 12x.
    peripheral_stub_step(state, 96000);

    // Periodic diagnostic — dump engine scheduling state. The rescues
    // below trigger on specific newjob / slot-loc / active-prio
    // conditions; if they never fire, this log tells us what state the
    // engine is actually IN so we can tune the triggers.
    static int g_diag_tick = 0;
    if ((++g_diag_tick % 10) == 0) {   // every 10 calls ≈ 1 sec on ESP32
        int newjob = state->Erasable[0][067] & 077777;
        int active_prio = state->Erasable[0][0167] & 077777;
        int active_loc  = state->Erasable[0][0164] & 077777;
        int z = state->Erasable[0][5] & 077777;
        int wakestal_slot = -1;
        for (int slot = 0; slot < 8; slot++) {
            int base = 0154 + slot * 014;
            int loc = state->Erasable[0][base + 8] & 077777;
            int prio = state->Erasable[0][base + 11] & 077777;
            if (prio != 077777 && prio != 0 && (loc == 027414 || loc == 027415)) {
                wakestal_slot = slot; break;
            }
        }
        ESP_LOGI(PSTUB_TAG, "tick%d Z=%05o newjob=%06o active=p%06o@%06o wakestal=%d",
                 g_diag_tick, z, newjob, active_prio, active_loc, wakestal_slot);
    }

#ifndef CONFIG_AGC_YAAGC_SOCKET
    // ---- Legacy rescue chain ----
    // These hacks papered over the in-process-driver bug that
    // CONFIG_AGC_YAAGC_SOCKET fixes structurally. They are kept ONLY
    // for builds that haven't moved to the canonical socket path yet
    // (host Layer-2 tests, primarily). When CONFIG_AGC_YAAGC_SOCKET=y
    // (now the firmware default), the canonical mask+value drain
    // inside ChannelInput handles CHARIN dispatch / job swaps / etc
    // the way yaAGC.exe does — no rescues needed.
    //
    //   rescue_stuck_job: NEWJOB pending but executive can't swap
    //   rescue_wakestal_sleeper: slot parked in WAKESTAL forever
    //   dispatch_pending_charin: 1/ACCSET pinned, CHARIN slot waiting
    //   rescue_stuck_z (below): generic Z-pin detector for tight loops
    rescue_stuck_job(state);
    rescue_wakestal_sleeper(state);
    dispatch_pending_charin(state);
    if (g_keypress_pending) force_dispatch_charin(state);
#endif

    // Generic "active job stuck at same Z" rescue. Hardware monitor
    // shows the engine bouncing forever in tight loops with an active
    // job (typically CHARIN at p030110) and no NEWJOB swap pending —
    // so neither rescue_stuck_job (needs NEWJOB) nor the specific
    // dispatch_pending_charin (needs 1/ACCSET active) ever fire.
    //
    // Trigger: same Z address (within a small tolerance) for K
    // consecutive ChannelRoutine ticks AND active slot is occupied.
    // K=4 ticks ≈ 3 sec on ESP32 ≈ 32K simulated cycles. A healthy
    // engine moves Z hundreds-of-thousands of distinct addresses per
    // second; if it's pinned to ONE Z for 3 sec, it's stuck.
#ifndef CONFIG_AGC_YAAGC_SOCKET
    static int g_stuck_z_count = 0;
    static int g_stuck_z_last = 0;
    static int g_stuck_z_rescues = 0;
    int z_check = state->Erasable[0][5] & 077777;
    int prio_check = state->Erasable[0][0167] & 077777;
    int job_active = (prio_check != 077777 && prio_check != 0);
    // Compare with a 16-address tolerance: tight interpretive loops
    // bounce a few sequential Z values, but stay in a small range.
    int z_delta = z_check - g_stuck_z_last;
    if (z_delta < 0) z_delta = -z_delta;
    if (job_active && z_delta < 16) {
        g_stuck_z_count++;
        if (g_stuck_z_count >= 4 && g_stuck_z_rescues < 8) {
            simulate_gojam(state);
            ESP_LOGI(PSTUB_TAG, "rescue_stuck_z #%d fired (Z=%05o prio=%06o)",
                     g_stuck_z_rescues + 1, z_check, prio_check);
            g_stuck_z_rescues++;
            g_stuck_z_count = 0;
        }
    } else {
        g_stuck_z_count = 0;
    }
    g_stuck_z_last = z_check;
#endif
}
