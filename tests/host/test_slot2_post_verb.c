// test_slot2_post_verb — sample slot 2 PRIORITY for 50k cycles after
// VERB keypress. Find when (if ever) slot 2 gets allocated to CHARIN
// (PRIO=30110, LOC=02077) and when it transitions to 77777 (freed).

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
    for (long c = 0; c < 1000000; c++) agc_engine(st);

    int s2_base = 0154 + 2*014;
    printf("before VERB: slot2 PRIO=%05o LOC=%05o\n",
           st->Erasable[0][s2_base+11] & 077777,
           st->Erasable[0][s2_base+8]  & 077777);

    harness_post_key(DSKY_KEY_VERB);

    int prev_prio = -1;
    int transitions = 0;
    for (long c = 0; c < 50000 && transitions < 40; c++) {
        agc_engine(st);
        int prio = st->Erasable[0][s2_base+11] & 077777;
        int loc  = st->Erasable[0][s2_base+8]  & 077777;
        if (prio != prev_prio) {
            transitions++;
            int z = st->Erasable[0][5] & 07777;
            printf("c=%5ld slot2 PRIO=%05o LOC=%05o (Z=%05o)\n",
                   c, prio, loc, z);
            prev_prio = prio;
        }
    }
    PASS();
}
