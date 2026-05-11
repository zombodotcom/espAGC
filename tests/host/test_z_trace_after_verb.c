// test_z_trace_after_verb — sample Z every cycle for 50k cycles after
// posting VERB. Count visits to key addresses:
//   02077 = CHARIN entry (bank 40)
//   02354 = VERB handler entry (bank 40)
//   02112 = CHARIN2 (somewhere after the lead-in)
//   01001 = VERBREG accesses (any TS/XCH there)
// Report a histogram of unique Z values visited.

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

    static int counts[010000];
    harness_post_key(DSKY_KEY_VERB);

    int last = -1;
    for (long c = 0; c < 50000; c++) {
        agc_engine(st);
        int z = st->Erasable[0][5] & 07777;
        if (z != last) counts[z]++;
        last = z;
    }

    // Specific addresses we care about:
    printf("after VERB +50k cycles:\n");
    printf("  Z=02077 (CHARIN):    %d visits\n", counts[02077]);
    printf("  Z=02100 (CHARIN+1):  %d visits\n", counts[02100]);
    printf("  Z=02101 (CHARIN+2):  %d visits\n", counts[02101]);
    printf("  Z=02112 (CHARIN2):   %d visits\n", counts[02112]);
    printf("  Z=02354 (VERB):      %d visits\n", counts[02354]);
    printf("  Z=02355 (VERB+1):    %d visits\n", counts[02355]);
    printf("  Z=02356 (VERB+2):    %d visits\n", counts[02356]);
    printf("\nTop 20 hottest in window:\n");
    int top_z[20] = {0}, top_n[20] = {0};
    for (int z = 0; z < 010000; z++) {
        if (counts[z] == 0) continue;
        int min = 0;
        for (int i = 1; i < 20; i++) if (top_n[i] < top_n[min]) min = i;
        if (counts[z] > top_n[min]) { top_n[min] = counts[z]; top_z[min] = z; }
    }
    for (int i = 0; i < 20; i++)
        for (int j = i+1; j < 20; j++)
            if (top_n[j] > top_n[i]) {
                int tz = top_z[i], tn = top_n[i];
                top_z[i] = top_z[j]; top_n[i] = top_n[j];
                top_z[j] = tz; top_n[j] = tn;
            }
    for (int i = 0; i < 20; i++)
        if (top_n[i] > 0)
            printf("  Z=%05o visits=%d\n", top_z[i], top_n[i]);
    PASS();
}
