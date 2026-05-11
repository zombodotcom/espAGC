// test_charin_state — read named erasable cells after V/3/5/E to
// pin down what CHARIN actually does. Addresses from /tmp/luminary.lst:
//   DSPCOUNT = 01000 -> e[2][000]
//   VERBREG  = 01001 -> e[2][001]
//   NOUNREG  = 01002 -> e[2][002]
//   XREG     = 01003 -> e[2][003]
//   YREG     = 01004 -> e[2][004]
//   ZREG     = 01005 -> e[2][005]
//   DECBRNCH = 01007 -> e[2][007]   (after XREG/YREG/ZREG/XREGLP/YREGLP/ZREGLP)
//   Actually DECBRNCH = 01000 -> let me re-look. ERASABLE_ASSIGNMENTS lines
//   512-514: DSPCOUNT, DECBRNCH, VERBREG.  So DSPCOUNT=01000, DECBRNCH=01001,
//   VERBREG=01001 -- conflict!  Listing says VERBREG=01001. So DECBRNCH is
//   defined before VERBREG?  Going with the listing.
//   DSPLOCK  = 01012 -> e[2][012]
//   CADRSTOR = 01042 -> e[2][042]

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
    int dspcount = st->Erasable[2][0]   & 077777;
    int verbreg  = st->Erasable[2][1]   & 077777;
    int nounreg  = st->Erasable[2][2]   & 077777;
    int dsplock  = st->Erasable[2][012] & 077777;
    int cadrstor = st->Erasable[2][042] & 077777;
    int mpac0    = st->Erasable[0][0154] & 077777;
    printf("%-18s DSPCOUNT=%05o VERBREG=%05o NOUNREG=%05o DSPLOCK=%05o CADRSTOR=%05o MPAC0=%05o\n",
           tag, dspcount, verbreg, nounreg, dsplock, cadrstor, mpac0);
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
