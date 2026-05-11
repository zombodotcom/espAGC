// test_very_long — run for 100M cycles after V35E key sequence,
// sampling state at intervals. See if 1/ACCS eventually completes
// and CHARIN runs.

#include "agc_harness.h"
#include "test_helpers.h"
#include "yaAGC.h"
#include "agc_engine.h"
#include "dsky_keys.h"
#include <stdio.h>

extern agc_t *agc_core_state(void);
extern int agc_engine(agc_t *st);

static void dump(const char *tag, agc_t *st)
{
    int dspcount = st->Erasable[2][0]   & 077777;
    int verbreg  = st->Erasable[2][1]   & 077777;
    int dsplock  = st->Erasable[2][012] & 077777;
    int newjob   = st->Erasable[0][067] & 077777;
    int s0_prio  = st->Erasable[0][0167] & 077777;
    int s1_prio  = st->Erasable[0][0203] & 077777;
    int s0_loc   = st->Erasable[0][0164] & 077777;
    int s1_loc   = st->Erasable[0][0200] & 077777;
    int prog_alarm = st->Erasable[2][0o36] & 0o400 ? 1 : 0;
    printf("%-15s VRB=%05o DSPC=%05o LOCK=%05o NJ=%05o "
           "s0=%05o/%05o s1=%05o/%05o PA=%d\n",
           tag, verbreg, dspcount, dsplock, newjob,
           s0_prio, s0_loc, s1_prio, s1_loc, prog_alarm);
}

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    // Long boot first.
    for (long c = 0; c < 10000000; c++) agc_engine(st);
    dump("10M boot", st);

    harness_post_key(DSKY_KEY_VERB);
    harness_post_key(3);
    harness_post_key(5);
    harness_post_key(DSKY_KEY_ENTR);

    // 10 x 10M = 100M cycles, sample at each 10M.
    for (int i = 0; i < 10; i++) {
        for (long c = 0; c < 10000000; c++) agc_engine(st);
        char tag[32];
        sprintf(tag, "+%dM", (i+1)*10);
        dump(tag, st);
    }

    dsky_state_t d;
    harness_snapshot(&d);
    printf("\nDSKY: PRG=[%d,%d] VRB=[%d,%d] NUN=[%d,%d] comp_acty=%d prog_alarm=%d\n",
           d.prog[0], d.prog[1], d.verb[0], d.verb[1], d.noun[0], d.noun[1],
           d.comp_acty, d.prog_alarm);
    PASS();
}
