// test_no_autorset_verb — same as test_slots_correct but with auto-RSET
// DISABLED at compile time. Theory: with no auto-RSET grabbing slot 0
// at boot, a later manual VERB keypress should land in slot 0 (or the
// lowest free slot above slot 0) AND its CHARIN should actually run
// to completion since nothing else competes at PRIO=30110.
//
// Build with: gcc ... (no -DCONFIG_AGC_AUTO_RSET_AT_BOOT)

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
    int ir5     = st->InterruptRequests[5];
    int ch015   = st->InputChannel[015] & 037;
    int newjob  = st->Erasable[0][0067] & 077777;
    printf("%-25s IR5=%d ch015=%02o NEWJOB=%05o\n", tag, ir5, ch015, newjob);
    printf("  slot [PRIO  LOC   BANK  MODE  PUSH  MPAC0]:\n");
    for (int s = 0; s < 8; s++) {
        int mpac_base = 0154 + s * 014;
        int prio = st->Erasable[0][mpac_base + 11] & 077777;
        int loc  = st->Erasable[0][mpac_base + 8]  & 077777;
        int bs   = st->Erasable[0][mpac_base + 9]  & 077777;
        int mode = st->Erasable[0][mpac_base + 7]  & 077777;
        int push = st->Erasable[0][mpac_base + 10] & 077777;
        int mpac0= st->Erasable[0][mpac_base + 0]  & 077777;
        if (prio || loc || bs || mode || mpac0)
            printf("    [%d] %05o %05o %05o %05o %05o %05o\n",
                   s, prio, loc, bs, mode, push, mpac0);
    }
    dsky_state_t d;
    harness_snapshot(&d);
    printf("  DSKY: PRG=[%d,%d] VRB=[%d,%d] NUN=[%d,%d] comp_acty=%d prog_alarm=%d restart=%d\n",
           d.prog[0], d.prog[1], d.verb[0], d.verb[1], d.noun[0], d.noun[1],
           d.comp_acty, d.prog_alarm, d.restart);
}

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    for (long c = 0; c < 1000000; c++) agc_engine(st);
    dump("1M cycles (NO auto-RSET)", st);

    harness_post_key(DSKY_KEY_VERB);
    for (long c = 0; c < 300000; c++) agc_engine(st);
    dump("After VERB +300k", st);

    harness_post_key(3);
    for (long c = 0; c < 300000; c++) agc_engine(st);
    dump("After 3 +300k", st);

    harness_post_key(5);
    for (long c = 0; c < 300000; c++) agc_engine(st);
    dump("After 5 +300k", st);

    harness_post_key(DSKY_KEY_ENTR);
    for (long c = 0; c < 1000000; c++) agc_engine(st);
    dump("After ENTR +1M", st);

    PASS();
}
