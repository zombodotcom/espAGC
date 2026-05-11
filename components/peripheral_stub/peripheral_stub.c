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

void peripheral_stub_init(void)
{
    // Nothing to do at init; tick is idempotent and self-correcting.
    // (Erasable-cell seeding was tried and found insufficient: Luminary's
    // FRESH-START rezeros DAPBOOLS, and the real fix needs a live
    // peripheral simulator like LM_Simulator on Pi/Linux — continuously
    // driving channels and CDU counter pulses, not a one-shot seed.)
}

void peripheral_stub_tick(agc_t *state)
{
    if (state == NULL) return;

    // (1) Restore peripheral channel baselines (ch031 and ch032 carry
    // stick / PRO-key state and are intentionally left alone).
    state->InputChannel[030] = CH030_BASELINE;
    state->InputChannel[033] = CH033_BASELINE;

    // Restore IMODES30/IMODES33 to fresh-start values so any fault
    // bits Luminary set on its last mode-switch pass go away before
    // the next ALARM check runs.
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
