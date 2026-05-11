// test_slot0_evolve — sample slot 0 PRIO+LOC at intervals from boot
// to identify when (if ever) it transitions states. Also captures Z
// each sample so we can spot stuck loops.

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

    int last_p = -1, last_l = -1, last_b = -1;
    long total = 0;
    for (long burst = 0; burst < 200; burst++) {
        for (long c = 0; c < 100000; c++) {
            agc_engine(st);
            total++;
            int p = st->Erasable[0][0167] & 077777;
            int l = st->Erasable[0][0164] & 077777;
            int b = st->Erasable[0][0165] & 077777;
            if (p != last_p || l != last_l || b != last_b) {
                int z = st->Erasable[0][5] & 07777;
                printf("c=%9ld slot0 PRIO=%05o LOC=%05o BANK=%05o Z=%05o\n",
                       total, p, l, b, z);
                last_p = p; last_l = l; last_b = b;
            }
        }
    }
    PASS();
}
