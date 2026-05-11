// test_ch010_writes — log every ch010 row write Luminary produces over
// 1M cycles + after a V35E keypress. ch010 = the DSKY relay rows. Each
// write encodes which row (4 bits) + an 11-bit payload that maps to
// digit segments / lamp state.
//
// If Luminary writes ch010 with sensible row+payload values, the
// channel_router decoder should turn those into dsky_state digits.
// If decoder isn't decoding them, the bug is in the decoder.
// If Luminary doesn't write them, the bug is upstream.

#include "agc_harness.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"
#include "dsky_keys.h"

#include <stdio.h>

extern agc_t *agc_core_state(void);
extern int agc_engine(agc_t *st);

// Track every ch010 write the engine produces.
// channel_router_on_output is the existing decode hook — we add a
// shadow logger via direct OutputChannel inspection.
static long ch010_writes_total = 0;
static int  ch010_per_row[16] = {0};

static void scan_ch010(agc_t *st)
{
    // The engine stores last-written ch010 in OutputChannel10[row].
    // Just iterate rows 0..11 and count any non-zero values.
    for (int row = 0; row < 16; row++) {
        if (st->OutputChannel10[row] != 0) ch010_per_row[row]++;
    }
}

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    for (long c = 0; c < 1000000; c++) {
        agc_engine(st);
        if ((c % 1000) == 0) scan_ch010(st);
    }

    printf("=== After 1M cycles boot ===\n");
    printf("OutputChannel10 contents by row (12-bit row + 11-bit payload):\n");
    for (int row = 0; row < 16; row++) {
        if (st->OutputChannel10[row] != 0)
            printf("  row %2d: %05o   (sampled non-zero %d times)\n",
                   row, st->OutputChannel10[row] & 077777, ch010_per_row[row]);
    }

    // Post V35E
    printf("\n=== Posting V, 3, 5, E ===\n");
    harness_post_key(DSKY_KEY_VERB);
    for (long c = 0; c < 300000; c++) agc_engine(st);
    harness_post_key(3);
    for (long c = 0; c < 300000; c++) agc_engine(st);
    harness_post_key(5);
    for (long c = 0; c < 300000; c++) agc_engine(st);
    harness_post_key(DSKY_KEY_ENTR);
    for (long c = 0; c < 1000000; c++) agc_engine(st);

    printf("\n=== After V35E sequence ===\n");
    for (int row = 0; row < 16; row++) {
        if (st->OutputChannel10[row] != 0)
            printf("  row %2d: %05o\n", row, st->OutputChannel10[row] & 077777);
    }
    dsky_state_t d;
    harness_snapshot(&d);
    printf("\nDSKY decoded: PRG=[%d,%d] VRB=[%d,%d] NUN=[%d,%d] "
           "comp_acty=%d prog_alarm=%d restart=%d\n",
           d.prog[0], d.prog[1], d.verb[0], d.verb[1], d.noun[0], d.noun[1],
           d.comp_acty, d.prog_alarm, d.restart);

    PASS();
}
