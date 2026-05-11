// test_slot0_origin — find what scheduled slot 0's persistent job.
// Snapshot slot 0 at increasing cycle counts to see when it first
// gets populated.

#include "agc_harness.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"

#include <stdio.h>

extern agc_t *agc_core_state(void);
extern int agc_engine(agc_t *st);

static void dump_slot(long cycle, agc_t *st)
{
    int mpac_base = 0154;  // slot 0
    int prio = st->Erasable[0][mpac_base + 11] & 077777;
    int loc  = st->Erasable[0][mpac_base + 8]  & 077777;
    int bs   = st->Erasable[0][mpac_base + 9]  & 077777;
    int mode = st->Erasable[0][mpac_base + 7]  & 077777;
    int push = st->Erasable[0][mpac_base + 10] & 077777;
    int mpac0= st->Erasable[0][mpac_base + 0]  & 077777;
    printf("  cycle %7ld  slot0 PRIO=%05o LOC=%05o BANK=%05o MODE=%05o PUSH=%05o MPAC0=%05o\n",
           cycle, prio, loc, bs, mode, push, mpac0);
}

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    long checkpoints[] = {
        100, 1000, 5000, 10000, 20000, 50000, 100000,
        110000, 120000, 125000, 128000, 130000, 131000, 131500,
        132000, 133000, 135000, 140000, 200000, 500000,
        1000000, 1500000
    };
    int nc = sizeof(checkpoints) / sizeof(checkpoints[0]);

    long cur = 0;
    for (int i = 0; i < nc; i++) {
        long target = checkpoints[i];
        while (cur < target) {
            agc_engine(st);
            cur++;
        }
        dump_slot(cur, st);
    }

    PASS();
}
