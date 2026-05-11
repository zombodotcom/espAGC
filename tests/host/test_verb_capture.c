// test_verb_capture — read VERBREG, NOUNREG, DSPCOUNT, DECBRNCH after
// each step of V/3/5/E. Confirms whether CHARIN is correctly storing
// the typed verb code.
//
// Erasable addresses from /tmp/luminary.lst:
//   VERBREG  = 01001 -> Erasable[2][001]
//   NOUNREG  = 01002 -> Erasable[2][002]
//   DSPCOUNT = 01000 -> Erasable[2][000]
//   DECBRNCH = 01000 ?  (line 513 ERASE after DSPCOUNT)
// DSPCOUNT @ ERASABLE_ASSIGNMENTS.agc:512 is right after MONSAVE2 group;
// VERBREG @ :514. So DSPCOUNT=01000, DECBRNCH=01001? Actually VERBREG=
// 01001 per listing, so DSPCOUNT=0777 (preceding cell). Look up.

#include "agc_harness.h"
#include "test_helpers.h"
#include "yaAGC.h"
#include "agc_engine.h"
#include "dsky_keys.h"
#include <stdio.h>

extern agc_t *agc_core_state(void);
extern int agc_engine(agc_t *st);

static void dump(const char *tag, agc_t *st)
{
    // Bank 2 offsets:  VERBREG=001, NOUNREG=002
    // Scan bank 2 offsets 0..7 to find DSPCOUNT/DECBRNCH/VERBREG/NOUNREG context.
    printf("%-22s Z=%05o ch015=%02o bank2[0..7]: ",
           tag, st->Erasable[0][5] & 07777,
           st->InputChannel[015] & 037);
    for (int i = 0; i < 8; i++) printf("%05o ", st->Erasable[2][i] & 077777);
    printf("\n");
}

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();
    for (long c = 0; c < 1000000; c++) agc_engine(st);
    dump("1M boot", st);

    harness_post_key(DSKY_KEY_VERB);
    for (long c = 0; c < 200000; c++) agc_engine(st);
    dump("after V", st);

    harness_post_key(3);
    for (long c = 0; c < 200000; c++) agc_engine(st);
    dump("after 3", st);

    harness_post_key(5);
    for (long c = 0; c < 200000; c++) agc_engine(st);
    dump("after 5", st);

    harness_post_key(DSKY_KEY_ENTR);
    for (long c = 0; c < 2000000; c++) agc_engine(st);
    dump("after E", st);

    PASS();
}
