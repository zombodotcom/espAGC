// test_find_dsplock — find DSPLOCK's actual erasable address by
// snapshotting all of bank 0 and bank 2 before and after a keypress,
// then reporting which cells changed. CHARIN's first instruction sets
// DSPLOCK to 1, so it must be among the changed cells if CHARIN runs.

#include "agc_harness.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"
#include "dsky_keys.h"

#include <stdio.h>
#include <string.h>

extern agc_t *agc_core_state(void);
extern int agc_engine(agc_t *st);

static int snap_bank0[0400];
static int snap_bank1[0400];
static int snap_bank2[0400];

static void snapshot_all(agc_t *st)
{
    for (int i = 0; i < 0400; i++) snap_bank0[i] = st->Erasable[0][i] & 077777;
    for (int i = 0; i < 0400; i++) snap_bank1[i] = st->Erasable[1][i] & 077777;
    for (int i = 0; i < 0400; i++) snap_bank2[i] = st->Erasable[2][i] & 077777;
}

static void diff(agc_t *st, const char *tag)
{
    printf("=== Diff after %s ===\n", tag);
    int changes = 0;
    for (int i = 0; i < 0400; i++) {
        int now = st->Erasable[0][i] & 077777;
        if (now != snap_bank0[i] && changes < 50) {
            printf("  Erasable[0][%04o]: %05o -> %05o  (addr=%04o)\n",
                   i, snap_bank0[i], now, i);
            changes++;
        }
    }
    for (int i = 0; i < 0400; i++) {
        int now = st->Erasable[1][i] & 077777;
        if (now != snap_bank1[i] && changes < 50) {
            int addr = 0400 + i;
            printf("  Erasable[1][%04o]: %05o -> %05o  (addr=%04o)\n",
                   i, snap_bank1[i], now, addr);
            changes++;
        }
    }
    for (int i = 0; i < 0400; i++) {
        int now = st->Erasable[2][i] & 077777;
        if (now != snap_bank2[i] && changes < 50) {
            int addr = 01000 + i;
            printf("  Erasable[2][%04o]: %05o -> %05o  (addr=%04o)\n",
                   i, snap_bank2[i], now, addr);
            changes++;
        }
    }
    if (changes == 50) printf("  (...truncated at 50 changes)\n");
    printf("  Total changes: %d\n", changes);
}

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    for (long c = 0; c < 1000000; c++) agc_engine(st);
    snapshot_all(st);
    printf("Snapshot taken at cycle 1M. Now posting VERB...\n\n");

    harness_post_key(DSKY_KEY_VERB);
    for (long c = 0; c < 100000; c++) agc_engine(st);
    diff(st, "VERB +100k");

    snapshot_all(st);
    printf("\nSnapshot. Now posting 3...\n\n");
    harness_post_key(3);
    for (long c = 0; c < 100000; c++) agc_engine(st);
    diff(st, "3 +100k");

    PASS();
}
