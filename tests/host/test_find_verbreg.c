// test_find_verbreg — instead of guessing addresses, capture the
// full erasable memory before and after V35E, and report cells that
// changed. The verb code 0o35 (octal 35 = decimal 29) should appear
// in some cell. If it's not VERBREG, dispatch is dropping it.

#include "agc_harness.h"
#include "test_helpers.h"
#include "yaAGC.h"
#include "agc_engine.h"
#include "dsky_keys.h"
#include <stdio.h>
#include <string.h>

extern agc_t *agc_core_state(void);
extern int agc_engine(agc_t *st);

static int snap[8][0400];

static void take_snap(agc_t *st)
{
    for (int b = 0; b < 8; b++)
        for (int o = 0; o < 0400; o++)
            snap[b][o] = st->Erasable[b][o] & 077777;
}

static void diff_snap(agc_t *st, const char *tag)
{
    int n = 0;
    printf("\n=== changes since previous snapshot: %s ===\n", tag);
    for (int b = 0; b < 8; b++) {
        for (int o = 0; o < 0400; o++) {
            int cur = st->Erasable[b][o] & 077777;
            if (cur != snap[b][o]) {
                if (n < 60) {
                    int addr = b * 0400 + o;
                    printf("  e[%d][%03o]=%05o (addr=%05o) was %05o\n",
                           b, o, cur, addr, snap[b][o]);
                }
                n++;
            }
        }
    }
    printf("  (%d cells changed)\n", n);
    take_snap(st);
}

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();
    for (long c = 0; c < 1000000; c++) agc_engine(st);

    take_snap(st);
    harness_post_key(DSKY_KEY_VERB);
    for (long c = 0; c < 200000; c++) agc_engine(st);
    diff_snap(st, "after VERB");

    harness_post_key(3);
    for (long c = 0; c < 200000; c++) agc_engine(st);
    diff_snap(st, "after 3");

    harness_post_key(5);
    for (long c = 0; c < 200000; c++) agc_engine(st);
    diff_snap(st, "after 5");

    harness_post_key(DSKY_KEY_ENTR);
    for (long c = 0; c < 2000000; c++) agc_engine(st);
    diff_snap(st, "after ENTR");

    PASS();
}
