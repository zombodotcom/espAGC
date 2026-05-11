// test_long_verb — much longer wait between operations so any
// slow-but-progressing chain has time to complete. Especially
// useful with InhibitAlarms=1 where slots aren't being wiped by
// GOJAMs.

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
    int s0_prio  = st->Erasable[0][0167] & 077777;
    int s1_prio  = st->Erasable[0][0203] & 077777;
    int dapbools = st->Erasable[0][0111] & 077777;
    // MASS = address 01244 -> Erasable[2][0244]. Per /tmp/luminary.lst
    // TS MASS opcode 55244 -> Address10 = 1244 = bank 2 offset 244.
    int mass     = st->Erasable[2][0244] & 077777;
    printf("%-25s VRB=%05o DSPC=%05o LCK=%05o s0=%05o s1=%05o DAP=%05o ACCSOKAY=%d MASS=%05o\n",
           tag, verbreg, dspcount, dsplock, s0_prio, s1_prio,
           dapbools, (dapbools & 4) ? 1 : 0, mass);
}

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();
    for (long c = 0; c < 5000000; c++) agc_engine(st);   // 5M cycles boot
    dump("5M boot", st);

    harness_post_key(DSKY_KEY_VERB);
    for (long c = 0; c < 5000000; c++) agc_engine(st);
    dump("after V +5M", st);

    harness_post_key(3);
    for (long c = 0; c < 5000000; c++) agc_engine(st);
    dump("after 3 +5M", st);

    harness_post_key(5);
    for (long c = 0; c < 5000000; c++) agc_engine(st);
    dump("after 5 +5M", st);

    harness_post_key(DSKY_KEY_ENTR);
    for (long c = 0; c < 10000000; c++) agc_engine(st);
    dump("after E +10M", st);

    dsky_state_t d;
    harness_snapshot(&d);
    printf("DSKY: PRG=[%d,%d] VRB=[%d,%d] NUN=[%d,%d] comp_acty=%d prog_alarm=%d\n",
           d.prog[0], d.prog[1], d.verb[0], d.verb[1], d.noun[0], d.noun[1],
           d.comp_acty, d.prog_alarm);
    PASS();
}
