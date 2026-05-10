// test_peripheral_stub_clears_imodes — verifies that peripheral_stub_tick()
// restores ch030/ch033 baselines and rewrites IMODES30/IMODES33 to
// their fresh-start values, regardless of what fault bits were set
// before the tick.
//
// See docs/superpowers/specs/2026-05-10-prog-alarm-watchdog-design.md.

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

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    // Stomp the channels and erasable mirrors with arbitrary fault
    // patterns to simulate Luminary having just written fault bits.
    st->InputChannel[030] = 000000;   // all "signal present" (everything failing)
    st->InputChannel[033] = 000000;   // AGC WARNING + PIPA FAIL + ...
    st->Erasable[IMODES30_BANK][IMODES30_OFFSET] = 077777;
    st->Erasable[IMODES33_BANK][IMODES33_OFFSET] = 077777;

    peripheral_stub_tick(st);

    printf("after tick: ch030=%06o ch033=%06o imodes30=%06o imodes33=%06o\n",
           st->InputChannel[030], st->InputChannel[033],
           st->Erasable[IMODES30_BANK][IMODES30_OFFSET],
           st->Erasable[IMODES33_BANK][IMODES33_OFFSET]);

    ASSERT(st->InputChannel[030] == CH030_BASELINE,
        "ch030 should be restored to CH030_BASELINE");
    ASSERT(st->InputChannel[033] == CH033_BASELINE,
        "ch033 should be restored to CH033_BASELINE");
    ASSERT(st->Erasable[IMODES30_BANK][IMODES30_OFFSET] == IMODES30_FRESH,
        "IMODES30 should be rewritten to fresh-start value");
    ASSERT(st->Erasable[IMODES33_BANK][IMODES33_OFFSET] == IMODES33_FRESH,
        "IMODES33 should be rewritten to fresh-start value");

    PASS();
}
