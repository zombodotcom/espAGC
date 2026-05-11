// test_charin_timeline — single-step diagnostic. Dumps key engine state
// at regular intervals so we can see the executive's behavior across
// boot and post-keypress. Tracks NW trips and what's running.

#include "agc_harness.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"

#include <stdio.h>

extern agc_t *agc_core_state(void);
extern int    agc_engine(agc_t *State);   // upstream

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    // Track NW trips and RestartLight transitions across single-stepped run.
    int nw_trips    = 0;
    int restarts    = 0;
    int prev_nwt    = 0;
    int prev_rl     = 0;
    int dsplock_max = 0;
    int last_logged_z = 0;
    int posted_verb = 0;

    printf("# cycles   Z     slot0_PRIO slot0_LOC NEWJOB DSPLOCK RestartL  NWtrips FAILREG[0]\n");

    for (long cycle = 0; cycle < 1500000; cycle++) {
        // Drive one engine cycle ourselves
        agc_engine(st);

        // Track NW trips
        int nwt = st->NightWatchmanTripped;
        int rl  = st->RestartLight;
        if (nwt && !prev_nwt) nw_trips++;
        if (rl && !prev_rl) restarts++;
        prev_nwt = nwt; prev_rl = rl;

        // Track DSPLOCK max seen
        int dsplock = st->Erasable[2][012] & 077777;
        if (dsplock > dsplock_max) dsplock_max = dsplock;

        // Post a manual VERB key at cycle 250000 (well past auto-RSET)
        if (cycle == 250000 && !posted_verb) {
            harness_post_key(/* VERB */ 17);
            posted_verb = 1;
            printf("=== posted VERB key at cycle %ld ===\n", cycle);
        }

        // Log every 25k cycles
        if (cycle % 25000 == 0) {
            int z = st->Erasable[0][5];
            int slot0_prio = st->Erasable[0][0167] & 077777;
            int slot0_loc  = st->Erasable[0][0163] & 077777;
            int newjob     = st->Erasable[0][0067] & 077777;
            int failreg0   = st->Erasable[0][0375] & 077777;
            printf("%8ld  %05o  %05o     %05o     %05o  %05o   %d        %d       %05o\n",
                   cycle, z, slot0_prio, slot0_loc, newjob, dsplock,
                   rl, nw_trips, failreg0);
        }

        // Early-exit if DSPLOCK transitions to 1 (CHARIN ran!)
        if (dsplock == 1 && cycle > 250000) {
            printf("=== DSPLOCK=1 at cycle %ld — CHARIN RAN! ===\n", cycle);
            break;
        }
    }

    printf("\nFinal: NW trips=%d  restarts=%d  DSPLOCK_max=%05o\n",
           nw_trips, restarts, dsplock_max & 077777);
    printf("Final state: RegZ=%05o RestartLight=%d FAILREG=[%05o,%05o,%05o]\n",
           st->Erasable[0][5], st->RestartLight,
           st->Erasable[0][0375] & 077777,
           st->Erasable[0][0376] & 077777,
           st->Erasable[0][0377] & 077777);

    PASS();
}
