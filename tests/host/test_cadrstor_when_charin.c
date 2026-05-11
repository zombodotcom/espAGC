// test_cadrstor_when_charin — sample CADRSTOR every cycle and report
// the value EACH TIME Z lands at 02102 (CCS CADRSTOR in CHARIN).
// If CADRSTOR != 0 there, CHARIN diverts via RELDSPON.

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
    harness_post_key(DSKY_KEY_VERB);

    int prev_z = -1;
    int n = 0;
    for (long c = 0; c < 200000 && n < 30; c++) {
        agc_engine(st);
        int z = st->Erasable[0][5] & 07777;
        if (z == 02102 && prev_z != 02102) {
            int cadrstor = st->Erasable[2][042] & 077777;
            int mpac0 = st->Erasable[0][0154] & 077777;
            int fb = st->Erasable[0][4] & 077777;  // RegFB at addr 4
            int bb = st->Erasable[0][6] & 077777;  // RegBB at addr 6
            int adjFB = (fb >> 10) & 037;
            printf("hit 02102: cycle=%ld FB=%05o adjFB=%02o CADRSTOR=%05o MPAC0=%05o BB=%05o\n",
                   c, fb, adjFB, cadrstor, mpac0, bb);
            n++;
        }
        prev_z = z;
    }
    PASS();
}
