// test_peripheral_stub_clears_imodes — unit test for peripheral_stub_tick():
//   1. Restores ch030/ch033 baselines and rewrites IMODES30/IMODES33
//      to their fresh-start values (always).
//   2. Clears DSPTAB+11D bit 9 and zeros FAILREG[0..2] when FAILREG[0]
//      holds the NIGHT WATCHMAN alarm code (01107) — host-side ERROR
//      equivalent for the boot-time NW transient.
//   3. Leaves DSPTAB+11D bit 9 and FAILREG alone when FAILREG[0] holds
//      any other alarm code (real alarms must remain visible).

#include "agc_harness.h"
#include "peripheral_stub.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"

#include <stdio.h>

extern agc_t *agc_core_state(void);

#define CH030_BASELINE   036377
#define CH033_BASELINE   077777
#define IMODES30_FRESH   037411
#define IMODES33_FRESH   016040
#define IMODES30_BANK    2
#define IMODES30_OFFSET  0302
#define IMODES33_BANK    2
#define IMODES33_OFFSET  0303

#define FAILREG_BANK     0
#define FAILREG0         0375
#define FAILREG1         0376
#define FAILREG2         0377
#define DSPTAB11D_BANK   2
#define DSPTAB11D_OFF    036
#define ALARM_NW         01107
#define BIT9             0400u
#define BIT4             020u   /* NO ATT  */
#define BIT6             0100u  /* GIMBAL LOCK */
#define BIT15            040000u

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    // ---- (1) Channel/IMODES baseline restoration ----
    st->InputChannel[030] = 000000;
    st->InputChannel[033] = 000000;
    st->Erasable[IMODES30_BANK][IMODES30_OFFSET] = 077777;
    st->Erasable[IMODES33_BANK][IMODES33_OFFSET] = 077777;
    // No alarm in FAILREG; tick should NOT touch DSPTAB+11D here.
    st->Erasable[FAILREG_BANK][FAILREG0] = 0;
    st->Erasable[FAILREG_BANK][FAILREG1] = 0;
    st->Erasable[FAILREG_BANK][FAILREG2] = 0;
    st->Erasable[DSPTAB11D_BANK][DSPTAB11D_OFF] = BIT4 | BIT6;  // existing NO ATT + GL

    peripheral_stub_tick(st);

    printf("part 1: ch030=%06o ch033=%06o imodes30=%06o imodes33=%06o dsp=%06o\n",
           st->InputChannel[030], st->InputChannel[033],
           st->Erasable[IMODES30_BANK][IMODES30_OFFSET],
           st->Erasable[IMODES33_BANK][IMODES33_OFFSET],
           st->Erasable[DSPTAB11D_BANK][DSPTAB11D_OFF]);

    ASSERT(st->InputChannel[030] == CH030_BASELINE, "ch030 should be restored");
    ASSERT(st->InputChannel[033] == CH033_BASELINE, "ch033 should be restored");
    ASSERT(st->Erasable[IMODES30_BANK][IMODES30_OFFSET] == IMODES30_FRESH,
        "IMODES30 should be rewritten to fresh-start value");
    ASSERT(st->Erasable[IMODES33_BANK][IMODES33_OFFSET] == IMODES33_FRESH,
        "IMODES33 should be rewritten to fresh-start value");
    ASSERT(st->Erasable[DSPTAB11D_BANK][DSPTAB11D_OFF] == (BIT4 | BIT6),
        "DSPTAB+11D must not be touched when FAILREG[0] is zero");

    // ---- (2) NW alarm latched -> host-side ERROR clears it ----
    st->Erasable[FAILREG_BANK][FAILREG0] = ALARM_NW;
    st->Erasable[FAILREG_BANK][FAILREG1] = 042;       // noise to verify zeroed
    st->Erasable[FAILREG_BANK][FAILREG2] = 077;       // noise
    st->Erasable[DSPTAB11D_BANK][DSPTAB11D_OFF] = BIT9 | BIT4;  // lamp on, NO ATT also on

    peripheral_stub_tick(st);

    unsigned dsp = st->Erasable[DSPTAB11D_BANK][DSPTAB11D_OFF];
    printf("part 2: FAILREG=[%06o,%06o,%06o]  DSPTAB+11D=%06o\n",
           st->Erasable[FAILREG_BANK][FAILREG0],
           st->Erasable[FAILREG_BANK][FAILREG1],
           st->Erasable[FAILREG_BANK][FAILREG2], dsp);

    ASSERT((dsp & BIT9) == 0, "host-side ERROR must clear DSPTAB+11D bit 9");
    ASSERT((dsp & BIT4) == BIT4, "host-side ERROR must preserve NO ATT (bit 4)");
    ASSERT((dsp & BIT15) == BIT15, "host-side ERROR must set request bit 15");
    ASSERT(st->Erasable[FAILREG_BANK][FAILREG0] == 0, "FAILREG[0] should be zeroed");
    ASSERT(st->Erasable[FAILREG_BANK][FAILREG1] == 0, "FAILREG[1] should be zeroed");
    ASSERT(st->Erasable[FAILREG_BANK][FAILREG2] == 0, "FAILREG[2] should be zeroed");

    // ---- (3) Non-NW alarm -> peripheral_stub leaves it alone ----
    int real_alarm = 01302;   // arbitrary non-NW code
    st->Erasable[FAILREG_BANK][FAILREG0] = real_alarm;
    st->Erasable[DSPTAB11D_BANK][DSPTAB11D_OFF] = BIT9;  // lamp on

    peripheral_stub_tick(st);

    dsp = st->Erasable[DSPTAB11D_BANK][DSPTAB11D_OFF];
    printf("part 3: FAILREG[0]=%06o  DSPTAB+11D=%06o\n",
           st->Erasable[FAILREG_BANK][FAILREG0], dsp);

    ASSERT(st->Erasable[FAILREG_BANK][FAILREG0] == real_alarm,
        "non-NW alarm must remain in FAILREG[0]");
    ASSERT((dsp & BIT9) == BIT9,
        "non-NW alarm must keep DSPTAB+11D bit 9 set so user sees real fault");

    PASS();
}
