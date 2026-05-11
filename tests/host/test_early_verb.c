// test_early_verb — post the V/3/5/E sequence VERY EARLY in boot,
// before slot 0 is allocated to a long-running PRIO=27110 background
// job. CHARIN will land in slot 0 (lowest free slot) and run before
// any other PRIO27 job arrives.

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
    int verbreg  = st->Erasable[2][1]   & 077777;
    int dspcount = st->Erasable[2][0]   & 077777;
    int s0_prio  = st->Erasable[0][0167] & 077777;
    int s1_prio  = st->Erasable[0][0203] & 077777;
    printf("%-22s VRB=%05o DSPC=%05o s0=%05o s1=%05o\n",
           tag, verbreg, dspcount, s0_prio, s1_prio);
}

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    // Only ~3000 cycles of boot — before slot 0 gets allocated at c≈3500.
    for (long c = 0; c < 3000; c++) agc_engine(st);
    dump("3k boot", st);

    harness_post_key(DSKY_KEY_VERB);
    for (long c = 0; c < 100000; c++) agc_engine(st);
    dump("after V +100k", st);

    harness_post_key(3);
    for (long c = 0; c < 100000; c++) agc_engine(st);
    dump("after 3 +100k", st);

    harness_post_key(5);
    for (long c = 0; c < 100000; c++) agc_engine(st);
    dump("after 5 +100k", st);

    harness_post_key(DSKY_KEY_ENTR);
    for (long c = 0; c < 1000000; c++) agc_engine(st);
    dump("after E +1M", st);

    dsky_state_t d;
    harness_snapshot(&d);
    printf("DSKY: PRG=[%d,%d] VRB=[%d,%d] NUN=[%d,%d]\n",
           d.prog[0], d.prog[1], d.verb[0], d.verb[1], d.noun[0], d.noun[1]);
    PASS();
}
