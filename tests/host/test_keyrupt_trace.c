// test_keyrupt_trace — single-step diagnostic around the auto-RSET window
// to find out WHY the KEYRUPT1 -> ERROR path isn't clearing DSPTAB+11D
// bit 9 (the PROG ALARM lamp).
//
// FAILREG diagnostic established the only alarm firing is 01107 NIGHT
// WATCHMAN, and ERROR routine should zero FAILREG + clear DSPTAB+11D
// bit 9 on RSET. Our peripheral_stub's KEYRUPT1 raise (InterruptRequests[5]=1
// via channel_router_pump_input) appears to not dispatch the ERROR routine.
//
// This test reaches into agc_engine state directly to capture:
//   - state->AllowInterrupt (interrupts enabled?)
//   - state->InIsr (already servicing an interrupt?)
//   - state->InterruptRequests[5] (pending KEYRUPT1?)
//   - state->InputChannel[015] (what keycode Luminary will read)
//   - state->Erasable[0][0375] (FAILREG[0])
//   - state->Erasable[2][0036] (DSPTAB+11D)
//   - state->Erasable[0][RegZ] (current program counter, to know where we are)
//
// Steps until just past tick 16 (auto-RSET fire), then single-steps and
// dumps every cycle. PASS unconditionally.

#include "agc_harness.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"

#include <stdio.h>

extern agc_t *agc_core_state(void);

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    // Step past the would-be auto-RSET fire point (tick 16 = ~131k cycles)
    // and a bit further so NW recovery has settled.
    harness_step(200000);

    // Snapshot right after the auto-RSET should have fired
    printf("=== POST-AUTO-RSET (cycle ~200k) ===\n");
    printf("  AllowInterrupt=%d  InIsr=%d  IntReq[5]=%d\n",
           st->AllowInterrupt, st->InIsr, st->InterruptRequests[5]);
    printf("  ch015=%06o  FAILREG[0]=%06o  DSPTAB+11D=%06o\n",
           st->InputChannel[015], st->Erasable[0][0375], st->Erasable[2][036]);
    printf("  RegZ=%06o  RestartLight=%d  WarningFilter=%d\n",
           st->Erasable[0][RegZ], st->RestartLight, st->WarningFilter);

    // Now post a fresh manual RSET and single-step for ~3000 cycles,
    // looking for state changes. Print only on change.
    printf("\n=== MANUAL RSET FOLLOW-UP ===\n");
    harness_post_key(/* DSKY_KEY_RSET */ 18);

    // DSPLOCK at erasable octal 01012 -> bank 2, offset 012. CHARIN
    // sets DSPLOCK to 1 on entry — if we see DSPLOCK transition from
    // 0 to 1, CHARIN ran.
    int prev_alm = st->AllowInterrupt;
    int prev_isr = st->InIsr;
    int prev_ir5 = st->InterruptRequests[5];
    int prev_ch15 = st->InputChannel[015];
    int prev_fr0 = st->Erasable[0][0375];
    int prev_dsp = st->Erasable[2][036];
    int prev_dsplock = st->Erasable[2][012];
    int prev_z = st->Erasable[0][RegZ];

    printf("# cycle  AI Iisr IR5 ch15  FR0   DSPTAB+11D DSPLOCK RegZ\n");
    printf("# %6d  %d  %d   %d   %05o %06o   %06o     %05o   %05o (init)\n",
           0, prev_alm, prev_isr, prev_ir5, prev_ch15, prev_fr0, prev_dsp,
           prev_dsplock, prev_z);

    int changes = 0;
    int saw_dsplock_set = 0;
    for (int c = 1; c <= 30000; c++) {
        harness_step(1);
        int alm = st->AllowInterrupt;
        int isr = st->InIsr;
        int ir5 = st->InterruptRequests[5];
        int ch15 = st->InputChannel[015];
        int fr0 = st->Erasable[0][0375];
        int dsp = st->Erasable[2][036];
        int dsplock = st->Erasable[2][012];
        int z   = st->Erasable[0][RegZ];

        bool change = (alm != prev_alm) || (isr != prev_isr) ||
                      (ir5 != prev_ir5) || (ch15 != prev_ch15) ||
                      (fr0 != prev_fr0) || (dsp != prev_dsp) ||
                      (dsplock != prev_dsplock);
        if (dsplock && !prev_dsplock) saw_dsplock_set++;
        if (change && changes < 120) {
            printf("# %6d  %d  %d   %d   %05o %06o   %06o     %05o   %05o\n",
                   c, alm, isr, ir5, ch15, fr0, dsp, dsplock, z);
            changes++;
        }
        prev_alm = alm; prev_isr = isr; prev_ir5 = ir5;
        prev_ch15 = ch15; prev_fr0 = fr0; prev_dsp = dsp;
        prev_dsplock = dsplock; prev_z = z;
    }
    if (changes >= 120) printf("... (truncated at 120 changes)\n");
    printf("DSPLOCK 0->1 transitions seen: %d\n", saw_dsplock_set);

    printf("\n=== FINAL ===\n");
    printf("  AllowInterrupt=%d  IntReq[5]=%d  FAILREG[0]=%06o  DSPTAB+11D=%06o\n",
           st->AllowInterrupt, st->InterruptRequests[5],
           st->Erasable[0][0375], st->Erasable[2][036]);
    dsky_state_t s;
    harness_snapshot(&s);
    printf("  dsky.prog_alarm=%d  dsky.restart=%d  comp_acty=%d\n",
           s.prog_alarm, s.restart, s.comp_acty);

    PASS();
}
