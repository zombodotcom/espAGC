// test_charin_trace — track slot 4 across boot + manual VERB keypress.
// When CHARIN is scheduled (we'll see slot N populated with PRIO=30110),
// log every PC visited while that slot is "active" so we can see what
// code actually runs.

#include "agc_harness.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"

#include <stdio.h>

extern agc_t *agc_core_state(void);
extern int    agc_engine(agc_t *State);

static int slot_prio(agc_t *st, int slot) {
    return st->Erasable[0][0154 + slot * 014 + 11] & 077777;
}
static int slot_loc(agc_t *st, int slot) {
    return st->Erasable[0][0154 + slot * 014 + 8] & 077777;
}

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    // Find first appearance of a slot at PRIO=030110 (CHARIN priority)
    int charin_slot = -1;
    long charin_appear_cycle = -1;

    for (long cycle = 0; cycle < 1500000; cycle++) {
        agc_engine(st);

        // Post manual VERB at cycle 200k (post-auto-RSET)
        if (cycle == 200000) {
            harness_post_key(/* VERB */ 17);
            printf("=== posted VERB at cycle %ld ===\n", cycle);
        }

        // Look for slot 4-7 having CHARIN priority (slot 0 already has stale 30110)
        if (charin_slot < 0) {
            for (int s = 1; s < 8; s++) {
                if (slot_prio(st, s) == 030110) {
                    charin_slot = s;
                    charin_appear_cycle = cycle;
                    printf("=== slot %d CHARIN-scheduled at cycle %ld: LOC=%05o ===\n",
                           s, cycle, slot_loc(st, s));
                    break;
                }
            }
        }

        // Watch DSPLOCK transition to 1
        int dsplock = st->Erasable[2][012] & 077777;
        if (dsplock == 1) {
            printf("!!! DSPLOCK=1 at cycle %ld RegZ=%05o !!!\n",
                   cycle, st->Erasable[0][5]);
            // Continue a bit more to see what happens next
            for (int more = 0; more < 200; more++) {
                agc_engine(st);
                printf("    cycle %ld+%d  Z=%05o A=%05o L=%05o  DSPLOCK=%05o\n",
                       cycle, more, st->Erasable[0][5],
                       st->Erasable[0][0] & 077777,
                       st->Erasable[0][1] & 077777,
                       st->Erasable[2][012] & 077777);
            }
            return 0;
        }

        // If slot transitioned from CHARIN-prio to FREE, log when
        if (charin_slot > 0 && slot_prio(st, charin_slot) == 077777) {
            printf("=== slot %d FREED at cycle %ld (no DSPLOCK trip) RegZ=%05o ===\n",
                   charin_slot, cycle, st->Erasable[0][5]);
            charin_slot = -1;  // look for next CHARIN scheduling
        }
    }

    printf("\nGave up after 1.5M cycles. CHARIN never dispatched.\n");
    PASS();
}
