// test_kill_slot0 — what if we forcibly free slot 0 (set its PRIORITY
// to -0)? Does the executive then dispatch the waiting CHARIN jobs in
// other slots, allowing DSKY updates?
//
// This is invasive and not a real fix, but it tells us whether the
// "slot 0 stuck" framing is correct. If freeing slot 0 unblocks
// keypresses, we know exactly where the bug is.

#include "agc_harness.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"
#include "dsky_keys.h"

#include <stdio.h>

extern agc_t *agc_core_state(void);
extern int agc_engine(agc_t *st);

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    // Run past boot transient.
    for (long c = 0; c < 1000000; c++) agc_engine(st);

    dsky_state_t d;
    harness_snapshot(&d);
    int slot0_prio = st->Erasable[0][0154 + 11] & 077777;
    int slot0_loc  = st->Erasable[0][0154 + 8]  & 077777;
    printf("Before kill: slot0 PRIO=%05o LOC=%05o, VRB=[%d,%d] PRG=[%d,%d]\n",
           slot0_prio, slot0_loc, d.verb[0], d.verb[1], d.prog[0], d.prog[1]);

    // Forcibly free slot 0 — set PRIORITY to -0 (077777). The AGC
    // executive convention is that -0 means "free slot."
    st->Erasable[0][0154 + 11] = 077777;
    // Also clear MPAC, LOC, BANKSET to avoid stale data confusing the
    // scheduler.
    for (int i = 0; i < 12; i++) st->Erasable[0][0154 + i] = (i == 11) ? 077777 : 0;
    // Restore PRIORITY = -0
    st->Erasable[0][0154 + 11] = 077777;
    printf("Slot 0 forcibly freed.\n");

    // Post a VERB keypress.
    harness_post_key(DSKY_KEY_VERB);
    for (long c = 0; c < 500000; c++) agc_engine(st);
    harness_snapshot(&d);
    slot0_prio = st->Erasable[0][0154 + 11] & 077777;
    slot0_loc  = st->Erasable[0][0154 + 8]  & 077777;
    printf("After kill+VERB+500k: slot0 PRIO=%05o LOC=%05o VRB=[%d,%d] PRG=[%d,%d] ca=%d rst=%d\n",
           slot0_prio, slot0_loc, d.verb[0], d.verb[1], d.prog[0], d.prog[1],
           d.comp_acty, d.restart);

    // Type 3 5 to make verb digits show "35"
    harness_post_key(3);
    for (long c = 0; c < 200000; c++) agc_engine(st);
    harness_post_key(5);
    for (long c = 0; c < 200000; c++) agc_engine(st);
    harness_post_key(DSKY_KEY_ENTR);
    for (long c = 0; c < 500000; c++) agc_engine(st);

    harness_snapshot(&d);
    printf("After V35E: VRB=[%d,%d] PRG=[%d,%d] comp_acty=%d prog_alarm=%d restart=%d\n",
           d.verb[0], d.verb[1], d.prog[0], d.prog[1],
           d.comp_acty, d.prog_alarm, d.restart);

    if (d.verb[0] == 3 && d.verb[1] == 5) {
        printf("\n*** SUCCESS *** VRB displays '35' after manual V35E keypress.\n");
        printf("CONCLUSION: slot 0's stuck job IS the blocker. Identifying it\n");
        printf("and finding why it doesn't yield is the actual fix.\n");
    } else {
        printf("\nFAIL: VRB digits still blank. Slot 0 isn't the only blocker.\n");
    }

    PASS();
}
