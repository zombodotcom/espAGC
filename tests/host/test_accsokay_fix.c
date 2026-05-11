// test_accsokay_fix — PROPOSED FIX TEST. Pre-set the ACCSOKAY bit in
// DAPBOOLS so DAPIDLER (DAP timer interrupt handler) thinks 1/ACCS
// is already complete. If this lets slot 0 stay free for CHARIN
// jobs, that's our fix.
//
// DAPBOOLS = FLGWRD13 = STATE + 13D = Erasable[0][0111] (octal).
// ACCSOKAY = BIT3 = mask 04 (LSB-indexed bit 3).
//
// The bit is normally set at the END of 1/ACCS (1/ACCRET routine).
// 1/ACCS does math on MASS, accelerations, etc. that we don't simulate.
// In the LM_Simulator world this state comes from external simulation.
// Here we just bypass by asserting the completion bit directly.

#include "agc_harness.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"
#include "dsky_keys.h"

#include <stdio.h>

extern agc_t *agc_core_state(void);
extern int agc_engine(agc_t *st);

#define DAPBOOLS_ADDR  0111
#define ACCSOKAY_BIT   04

static void dump(const char *tag, agc_t *st)
{
    int dapbools = st->Erasable[0][DAPBOOLS_ADDR] & 077777;
    int slot0_prio = st->Erasable[0][0154 + 11] & 077777;
    int slot0_loc  = st->Erasable[0][0154 + 8]  & 077777;
    int newjob = st->Erasable[0][0067] & 077777;
    dsky_state_t d;
    harness_snapshot(&d);
    printf("%-25s DAPBOOLS=%05o slot0[PRIO=%05o LOC=%05o] NEWJOB=%05o "
           "VRB=[%d,%d] PRG=[%d,%d] ca=%d pa=%d rst=%d\n",
           tag, dapbools, slot0_prio, slot0_loc, newjob,
           d.verb[0], d.verb[1], d.prog[0], d.prog[1],
           d.comp_acty, d.prog_alarm, d.restart);
}

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    // Step through boot up to where DAPIDLER would first run (some
    // T5RUPT after fresh start).
    for (long c = 0; c < 50000; c++) agc_engine(st);
    dump("@50k cycles", st);

    // Set ACCSOKAY bit so DAPIDLER thinks 1/ACCS is done.
    st->Erasable[0][DAPBOOLS_ADDR] |= ACCSOKAY_BIT;
    dump("ACCSOKAY set", st);

    // Run past boot transient.
    for (long c = 0; c < 1000000; c++) agc_engine(st);
    dump("+1M cycles", st);

    // Now post keypresses and see if DSKY responds.
    harness_post_key(DSKY_KEY_VERB);
    for (long c = 0; c < 300000; c++) agc_engine(st);
    dump("After VERB+300k", st);

    harness_post_key(3);
    for (long c = 0; c < 200000; c++) agc_engine(st);
    harness_post_key(5);
    for (long c = 0; c < 200000; c++) agc_engine(st);
    harness_post_key(DSKY_KEY_ENTR);
    for (long c = 0; c < 1000000; c++) agc_engine(st);
    dump("After V35E", st);

    dsky_state_t d;
    harness_snapshot(&d);
    if (d.verb[0] == 3 && d.verb[1] == 5) {
        printf("\n*** SUCCESS *** VRB shows '35' after V35E with ACCSOKAY fix.\n");
        printf("The fix works: set DAPBOOLS bit 3 at boot or as a peripheral feed.\n");
    } else {
        printf("\nVRB still blank. ACCSOKAY isn't sufficient alone.\n");
        printf("(May need to also clear or set other DAP-related state.)\n");
    }

    PASS();
}
