// test_charin_arrival — sample every cycle after VERB, capture
// the moment KEYRUPT1 stores keycode (TS MPAC) and slot becomes
// PRIO=30110 CHRPRIO. Check what slot's MPAC[0] holds, what active
// MPAC[0] holds, etc.
//
// In particular: when slot N's PRIORITY first transitions to 30110,
// what's in MPAC[0] of THAT slot?

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
    for (long c = 0; c < 5000000; c++) agc_engine(st);

    // Sample slot 0..7 prio. Watch for any slot transitioning to 30110.
    int prev_prio[8];
    for (int s = 0; s < 8; s++)
        prev_prio[s] = st->Erasable[0][0154 + s*014 + 11] & 077777;

    harness_post_key(DSKY_KEY_VERB);
    int verb_arrivals = 0;
    for (long c = 0; c < 5000000 && verb_arrivals < 20; c++) {
        agc_engine(st);
        for (int s = 0; s < 8; s++) {
            int prio = st->Erasable[0][0154 + s*014 + 11] & 077777;
            if (prio == 030110 && prev_prio[s] != 030110) {
                int loc  = st->Erasable[0][0154 + s*014 + 8]  & 077777;
                int bank = st->Erasable[0][0154 + s*014 + 9]  & 077777;
                int mpac0 = st->Erasable[0][0154 + s*014 + 0]  & 077777;
                int active_mpac0 = st->Erasable[0][0154] & 077777;
                int z = st->Erasable[0][5] & 07777;
                printf("c=%6ld slot %d: PRIO->30110 LOC=%05o BANK=%05o "
                       "slotN_MPAC0=%05o active_MPAC0=%05o Z=%05o\n",
                       c, s, loc, bank, mpac0, active_mpac0, z);
                verb_arrivals++;
            }
            prev_prio[s] = prio;
        }
    }

    // Sample slot 2 over the next 20k cycles to see what really happens.
    int last_p2 = -1, last_l2 = -1;
    int events = 0;
    for (long c = 0; c < 20000 && events < 20; c++) {
        agc_engine(st);
        int p = st->Erasable[0][0154 + 2*014 + 11] & 077777;
        int l = st->Erasable[0][0154 + 2*014 + 8]  & 077777;
        if (p != last_p2 || l != last_l2) {
            int b = st->Erasable[0][0154 + 2*014 + 9]  & 077777;
            int m = st->Erasable[0][0154 + 2*014 + 0]  & 077777;
            int z = st->Erasable[0][5] & 07777;
            printf("  c=%5ld slot2 PRIO=%05o LOC=%05o BANK=%05o MPAC0=%05o Z=%05o\n",
                   c, p, l, b, m, z);
            last_p2 = p; last_l2 = l;
            events++;
        }
    }

    // Also check final state
    int verbreg = st->Erasable[2][1] & 077777;
    int dspcount = st->Erasable[2][0] & 077777;
    printf("\nFinal: VERBREG=%05o DSPCOUNT=%05o\n", verbreg, dspcount);
    PASS();
}
