// test_early_verb_trace — same scenario as test_early_verb but trace
// whether CHARIN code (bank 40 Z=02077) actually executes.

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
    for (long c = 0; c < 3000; c++) agc_engine(st);

    harness_post_key(DSKY_KEY_VERB);

    int charin_hits = 0, charin2_hits = 0, verb_hits = 0;
    for (long c = 0; c < 1000000; c++) {
        agc_engine(st);
        int z = st->Erasable[0][5] & 07777;
        int fb = st->Erasable[0][4] & 077777;
        int adjFB = (fb >> 10) & 037;
        int sb = (st->OutputChannel7 & 0100) ? 1 : 0;
        int physBank = adjFB;
        if ((adjFB & 030) == 030 && sb) physBank += 010;
        if (physBank == 040) {
            if (z == 02077) charin_hits++;
            if (z == 02112) charin2_hits++;
            if (z == 02354) verb_hits++;
        }
    }
    printf("In 1M cycles after early VERB key (FB=040 required):\n");
    printf("  CHARIN entries (Z=02077): %d\n", charin_hits);
    printf("  CHARIN2 hits  (Z=02112): %d\n", charin2_hits);
    printf("  VERB handler  (Z=02354): %d\n", verb_hits);
    printf("  VERBREG final: %05o\n", st->Erasable[2][1] & 077777);
    PASS();
}
