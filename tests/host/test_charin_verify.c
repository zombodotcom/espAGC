// test_charin_verify — at every cycle, sample Z. If Z=02077, print
// FB, OutputChannel7, and computed physBank. Verify whether superbank
// bit is set when slot 2 should be running CHARIN.

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

    // Sample for 10000 cycles right after VERB. Print every Z=02077.
    int prev_z = -1;
    int n = 0;
    for (long c = 0; c < 10000 && n < 30; c++) {
        agc_engine(st);
        int z = st->Erasable[0][5] & 07777;
        if (z == 02077 && prev_z != 02077) {
            int fb = st->Erasable[0][4] & 077777;
            int adjFB = (fb >> 10) & 037;
            int ch7 = st->OutputChannel7 & 077777;
            int sb = (ch7 & 0100) ? 1 : 0;
            int physBank = adjFB;
            if ((adjFB & 030) == 030 && sb) physBank += 010;
            printf("c=%5ld Z=02077 FB=%05o ch7=%05o sb=%d adjFB=%02o physBank=%02o\n",
                   c, fb, ch7, sb, adjFB, physBank);
            n++;
        }
        prev_z = z;
    }
    printf("Total Z=02077 entries: %d\n", n);
    PASS();
}
