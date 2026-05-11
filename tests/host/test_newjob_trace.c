// test_newjob_trace — sample NEWJOB every cycle after VERB. Report every
// non-zero transition. NEWJOB at e[0][067]. If NOVAC sets NEWJOB=014
// (slot 1's LOCCTR), the executive should pick slot 1 on next ADVAN.

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
    int last_nj = -1;
    int events = 0;
    for (long c = 0; c < 100000 && events < 30; c++) {
        agc_engine(st);
        int nj = st->Erasable[0][067] & 077777;
        if (nj != last_nj) {
            int z = st->Erasable[0][5] & 07777;
            int s1p = st->Erasable[0][0203] & 077777;
            printf("c=%5ld NEWJOB=%05o (slot1_PRIO=%05o Z=%05o)\n",
                   c, nj, s1p, z);
            last_nj = nj;
            events++;
        }
    }
    PASS();
}
