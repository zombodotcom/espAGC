// test_v37_slots — after V37E + 0/0/E, dump all slot state to see if
// the program-digit keys are getting CHARIN-allocated.

#include "agc_harness.h"
#include "test_helpers.h"
#include "yaAGC.h"
#include "agc_engine.h"
#include <stdio.h>

extern agc_t *agc_core_state(void);

static void dump(const char *tag, agc_t *s)
{
    printf("--- %s ---\n", tag);
    printf("  RegZ=%05o RegFB=%05o RegBB=%05o ch7=%05o\n",
           s->Erasable[0][5] & 07777, s->Erasable[0][4] & 077777,
           s->Erasable[0][6] & 077777, s->OutputChannel7);
    int verbreg = s->Erasable[2][1] & 077777;
    int dspcount = s->Erasable[2][0] & 077777;
    int modreg = s->Erasable[0][6] & 077777;
    int dsplock = s->Erasable[2][012] & 077777;
    int mmnumber = s->Erasable[2][010] & 077777;
    printf("  VERBREG=%05o DSPCOUNT=%05o MODREG=%05o DSPLOCK=%05o MMNUMBER=%05o\n",
           verbreg, dspcount, modreg, dsplock, mmnumber);
    for (int slot = 0; slot < 8; slot++) {
        int base = 0154 + slot * 014;
        int loc = s->Erasable[0][base + 8] & 077777;
        int bset = s->Erasable[0][base + 9] & 077777;
        int prio = s->Erasable[0][base + 11] & 077777;
        if (prio != 0 && prio != 077777) {
            printf("  slot%d: LOC=%05o BSET=%05o PRIO=%05o\n",
                   slot, loc, bset, prio);
        }
    }
}

int main(void)
{
    harness_boot();
    harness_step(200000);
    harness_type("R", 50000);
    harness_type("V37E", 200000);
    dump("after V37E", agc_core_state());

    harness_post_key(0);  harness_step(50000);
    dump("after 1st 0 +50k", agc_core_state());

    harness_step(200000);
    dump("after 1st 0 +250k total", agc_core_state());

    harness_post_key(0);  harness_step(200000);
    dump("after 2nd 0", agc_core_state());

    harness_post_key(28); harness_step(500000);
    dump("after E", agc_core_state());

    PASS();
}
