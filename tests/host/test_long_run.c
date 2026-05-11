// test_long_run — run 20M cycles. Does slot 0's CHARIN ever free?
// Does anything ever cause the DSKY to display digits?

#include "agc_harness.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"

#include <stdio.h>

extern agc_t *agc_core_state(void);
extern int agc_engine(agc_t *st);

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    long checkpoints_M[] = {1, 2, 5, 10, 15, 20};
    int nc = sizeof(checkpoints_M) / sizeof(checkpoints_M[0]);
    long cur = 0;

    for (int i = 0; i < nc; i++) {
        long target = checkpoints_M[i] * 1000000L;
        while (cur < target) {
            agc_engine(st);
            cur++;
        }
        int p0 = st->Erasable[0][0154 + 11] & 077777;
        int p1 = st->Erasable[0][0170 + 11] & 077777;
        int p2 = st->Erasable[0][0204 + 11] & 077777;
        int newjob = st->Erasable[0][0067] & 077777;
        dsky_state_t d;
        harness_snapshot(&d);
        printf("@%2ldM: slot0_prio=%05o slot1_prio=%05o slot2_prio=%05o NEWJOB=%05o "
               "VRB=[%d,%d] PRG=[%d,%d] ca=%d pa=%d rst=%d\n",
               checkpoints_M[i], p0, p1, p2, newjob,
               d.verb[0], d.verb[1], d.prog[0], d.prog[1],
               d.comp_acty, d.prog_alarm, d.restart);
    }

    PASS();
}
