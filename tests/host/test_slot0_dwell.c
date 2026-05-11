// test_slot0_dwell — sample slot 0 PRIORITY every cycle for 100k cycles
// post-boot. Distribution tells us if slot 0 is *constantly* held at
// 33002 (MAKEPLAY runs back-to-back) or cycles through empty (77777)
// between jobs. Also samples slot 4's PRIORITY to see if CHARIN ever
// gets a turn after a VERB keypress.

#include "agc_harness.h"
#include "test_helpers.h"
#include "yaAGC.h"
#include "agc_engine.h"
#include "dsky_keys.h"
#include <stdio.h>

extern agc_t *agc_core_state(void);
extern int agc_engine(agc_t *st);

static void sample(agc_t *st, const char *tag, long n)
{
    long s0_33002 = 0, s0_77777 = 0, s0_other = 0;
    long s4_77777 = 0, s4_30110 = 0, s4_other = 0;
    int  s0_min = 0777777, s0_max = 0;
    int  s4_min = 0777777, s4_max = 0;
    int  s4_loc_seen[8] = {0};
    int  s4_loc[8]; int s4_loc_n = 0;

    for (long c = 0; c < n; c++) {
        agc_engine(st);
        int p0 = st->Erasable[0][0154 + 0*014 + 11] & 077777;
        int p4 = st->Erasable[0][0154 + 4*014 + 11] & 077777;
        int l4 = st->Erasable[0][0154 + 4*014 + 8]  & 077777;
        if (p0 < s0_min) s0_min = p0; if (p0 > s0_max) s0_max = p0;
        if (p4 < s4_min) s4_min = p4; if (p4 > s4_max) s4_max = p4;
        if (p0 == 033002)      s0_33002++;
        else if (p0 == 077777) s0_77777++;
        else                   s0_other++;
        if (p4 == 077777)      s4_77777++;
        else if (p4 == 030110) s4_30110++;
        else                   s4_other++;
        // Track distinct slot4 LOC values seen.
        if (p4 != 077777) {
            int dup = 0;
            for (int i = 0; i < s4_loc_n; i++) if (s4_loc[i] == l4) { dup = 1; break; }
            if (!dup && s4_loc_n < 8) s4_loc[s4_loc_n++] = l4;
        }
    }
    printf("\n=== %s (%ld cycles) ===\n", tag, n);
    printf("  slot0  PRIO=33002: %6ld   PRIO=77777: %6ld   other: %6ld   range[%05o..%05o]\n",
           s0_33002, s0_77777, s0_other, s0_min, s0_max);
    printf("  slot4  PRIO=77777: %6ld   PRIO=30110: %6ld   other: %6ld   range[%05o..%05o]\n",
           s4_77777, s4_30110, s4_other, s4_min, s4_max);
    printf("  slot4 distinct LOCs seen (%d):", s4_loc_n);
    for (int i = 0; i < s4_loc_n; i++) printf(" %05o", s4_loc[i]);
    printf("\n");
}

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();
    for (long c = 0; c < 1000000; c++) agc_engine(st);
    sample(st, "boot+1M, sample 100k", 100000);

    harness_post_key(DSKY_KEY_VERB);
    sample(st, "VERB key, sample 200k", 200000);

    PASS();
}
