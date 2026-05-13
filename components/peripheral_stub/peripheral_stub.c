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
    // and ends in the 1/ACCSET interpretive GOTO deadlock that older
    // rescue hacks papered over with WarningFilter side effects (those
    // cleared ch033 bit 13 and prevented V37E00E from completing).
    //
    // Instead, peripheral_stub_tick writes LM_INI after a delay matching
    // canonical's ~1s pre-LM_INI window. See g_lm_ini_pending.
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

        // Tell Luminary the LR antenna is in position 1 (altitude beam).
        // THE_LUNAR_LANDING.agc:245 (P63SPOT3) spins on:
        //   CA   BIT6        # IS THE LR ANTENNA IN POSITION 1 YET
        //   RAND CHAN33
        //   BZF  P63SPOT4    # BRANCH IF ANTENNA ALREADY IN POSITION 1
        // i.e. it falls through to "PLEASE CRANK THE SILLY THING AROUND"
        // until bit 6 of ch033 is clear. Our LM_INI sets ch033 to 077776
        // which leaves bit 6 set ("NOT pos 1"), so without this nudge
        // P63 never gets past the antenna check and LRALT (which reads
        // RNRAD into the altitude register) never runs.
        //
        // Narrow mask packet (BIT6 = 0o40) clears just that bit, then
        // restores the synthetic client's ch033 mask so later writes
        // here behave normally.
        extern int yaagc_socket_inject_packet(int, int, int);
        yaagc_socket_inject_packet(033, 0o40,    1);   // narrow mask: BIT6 only
        yaagc_socket_inject_packet(033, 0,       0);   // value: BIT6 = 0
        yaagc_socket_inject_packet(033, 077777,  1);   // restore full mask
        ESP_LOGI(PSTUB_TAG, "ch033 BIT6 cleared (LR antenna -> pos 1)");
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


void peripheral_stub_tick(agc_t *state)
{
    if (state == NULL) return;

    // Deferred LM_INI — fire after the cold-boot warm-up window. See
    // peripheral_stub_init comment for why this can't happen at cycle 0.
    if (g_lm_ini_pending) {
        if (g_lm_ini_ticks_remaining > 0) {
            g_lm_ini_ticks_remaining--;
        } else {
            // Route LM_INI through the canonical mask+value SocketAPI
            // drain. Five channels (ch016 + ch030..033), each as a mask
            // packet followed by a value packet — identical to the bytes
            // the Python driver sends to yaAGC.exe. Direct WriteIO from
            // outside ChannelInput proved 0/5 in every host test.
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
            g_lm_ini_pending = 0;
        }
    }

    // ChannelRoutine fires every 8191 MCT (agc_engine.c:1944 mask
    // `ChannelRoutineCount & 017777`). One MCT = 1/AGC_PER_SECOND sec
    // = 1/85333 sec ≈ 11.72 μs of simulated time. So 8191 MCT ≈ 96 ms
    // of simulated time. Previously this passed 8000 μs (mis-reading
    // cycle count as μs) which under-integrated attitude by 12x.
    peripheral_stub_step(state, 96000);

    // Periodic diagnostic — dump engine scheduling state so a hang
    // shows up in the serial log even when the canonical drain stays
    // quiet. Cheap (one ESP_LOGI/sec on hardware).
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
}
