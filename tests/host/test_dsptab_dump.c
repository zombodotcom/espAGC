// test_dsptab_dump — read DSPTAB cells (Erasable[2][023..036]) at boot,
// after V/3/5/E, and observe whether ANY display routine loads digit
// data. Also tracks DSPCOUNT, NOUT, VERBREG, NOUNREG.
//
// DSPTAB layout per Luminary099 ERASABLE_ASSIGNMENTS.agc:
//   DSPTAB+0..+10D : digit rows 1-11 (R3D5,R3D4..R3D2, R3D1,R2D5..R2D1,
//                                     R1D5..R1D1, NOUN, VERB, PROG)
//   DSPTAB+11D     : lamps / flag word
// Display routines like VBTSTLTS load these cells; DSPOUT reads them
// and pushes them onto ch010 row-by-row.

#include "agc_harness.h"
#include "test_helpers.h"
#include "yaAGC.h"
#include "agc_engine.h"
#include "dsky_keys.h"
#include <stdio.h>

extern agc_t *agc_core_state(void);
extern int agc_engine(agc_t *st);

// Look up addresses from ERASABLE_ASSIGNMENTS.agc + /tmp/luminary.lst:
//   DSPTAB at 01023 → bank 2 offset 023
//   DSPCOUNT mentioned in source — find via grep
//   NOUT (line 533 of ERASABLE_ASSIGNMENTS.agc)
//   VERBREG, NOUNREG
// To keep this test self-contained, dump bank 2 cells 020..050 (DSPTAB
// region) and a fixed slice of bank 0 for VERBREG/NOUNREG (bank 0 area).
static void dump(const char *tag, agc_t *st)
{
    printf("\n=== %s ===\n", tag);
    printf("  Z=%05o  ch015=%02o  ch010_last_writes:\n",
           st->Erasable[0][5] & 07777,
           st->InputChannel[015] & 037);
    printf("  DSPTAB (bank 2, offset 023..036):\n  ");
    for (int i = 0; i <= 13; i++) {
        printf(" %05o", st->Erasable[2][023 + i] & 077777);
        if ((i % 7) == 6) printf("\n  ");
    }
    printf("\n");
    // OutputChannel10 (per-row latest write).
    printf("  OutputChannel10 (last write per row 0..14):\n  ");
    for (int r = 0; r <= 14; r++) {
        printf(" %05o", st->OutputChannel10[r] & 077777);
        if ((r % 5) == 4) printf("\n  ");
    }
    printf("\n");
}

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();
    for (long c = 0; c < 2000000; c++) agc_engine(st);
    dump("boot+2M", st);

    harness_post_key(DSKY_KEY_VERB);
    for (long c = 0; c < 300000; c++) agc_engine(st);
    dump("After VERB +300k", st);

    harness_post_key(3);
    for (long c = 0; c < 300000; c++) agc_engine(st);
    dump("After 3 +300k", st);

    harness_post_key(5);
    for (long c = 0; c < 300000; c++) agc_engine(st);
    dump("After 5 +300k", st);

    harness_post_key(DSKY_KEY_ENTR);
    for (long c = 0; c < 3000000; c++) agc_engine(st);
    dump("After ENTR +3M", st);

    PASS();
}
