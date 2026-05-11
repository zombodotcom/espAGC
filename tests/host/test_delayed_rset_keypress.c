// test_delayed_rset_keypress — does delaying RSET past the boot transient
// let later keypresses actually dispatch CHARIN?
//
// Prior session showed: auto-RSET at ~131k cycles allocates slot 0 to a
// CHARIN-priority job that never executes, and all later keypresses
// pile up at the same priority. If we delay RSET until well past the
// transient (e.g. cycle 1M), the slot-0 state might be such that the
// keypress actually dispatches CHARIN normally.

#include "agc_harness.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"
#include "dsky_keys.h"

#include <stdio.h>

extern agc_t *agc_core_state(void);
extern int    agc_engine(agc_t *State);

#define DSPLOCK_BANK 2
#define DSPLOCK_OFF  012

static void print_snapshot(const char *tag, agc_t *st)
{
    int dsplock    = st->Erasable[DSPLOCK_BANK][DSPLOCK_OFF] & 077777;
    int newjob     = st->Erasable[0][0067] & 077777;
    int ir5        = st->InterruptRequests[5];
    int ch015      = st->InputChannel[015];
    int in_isr     = st->InIsr;
    int allow      = st->AllowInterrupt;
    dsky_state_t d;
    harness_snapshot(&d);
    printf("%-30s IR5=%d ch015=%05o NEWJOB=%05o DSPLOCK=%05o "
           "ca=%d pa=%d rst=%d\n",
           tag, ir5, ch015 & 037, newjob, dsplock,
           d.comp_acty, d.prog_alarm, d.restart);
    printf("  slots [PRIO  LOC  BANKSET  MODE]:\n");
    for (int s = 0; s < 8; s++) {
        int base = 0167 + s * 014;
        int prio = st->Erasable[0][base + 11] & 077777;
        int loc  = st->Erasable[0][base + 8]  & 077777;
        int bs   = st->Erasable[0][base + 9]  & 077777;
        int mode = st->Erasable[0][base + 7]  & 077777;
        if (prio || loc || bs || mode)
            printf("    [%d] %05o %05o %05o %05o\n", s, prio, loc, bs, mode);
    }
}

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    // Note: the build still has CONFIG_AGC_AUTO_RSET_AT_BOOT defined, so
    // channel_router will fire its own auto-RSET at ~131k. We can't
    // easily suppress that here without a Kconfig variant. So this test
    // documents what happens *after* the auto-RSET, and what later
    // manual keypresses do.

    for (long c = 0; c < 200000; c++) agc_engine(st);
    print_snapshot("200k cycles (post-auto-RSET)", st);

    for (long c = 0; c < 800000; c++) agc_engine(st);
    print_snapshot("1M cycles (settled)", st);

    // Now try a manual VERB keypress at 1M.
    harness_post_key(DSKY_KEY_VERB);
    for (long c = 0; c < 200000; c++) agc_engine(st);
    print_snapshot("After manual VERB+200k", st);

    // Type 3, 5, E (V35 = lamp test).
    harness_post_key(3);
    for (long c = 0; c < 200000; c++) agc_engine(st);
    print_snapshot("After 3 +200k", st);

    harness_post_key(5);
    for (long c = 0; c < 200000; c++) agc_engine(st);
    print_snapshot("After 5 +200k", st);

    harness_post_key(DSKY_KEY_ENTR);
    for (long c = 0; c < 500000; c++) agc_engine(st);
    print_snapshot("After ENTR +500k", st);

    // Try one more RSET in case the prior keys re-set the alarm.
    harness_post_key(DSKY_KEY_RSET);
    for (long c = 0; c < 200000; c++) agc_engine(st);
    print_snapshot("After RSET +200k", st);

    PASS();
}
