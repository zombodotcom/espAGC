// test_keypress_timeline — sample IR5, ch015, and slot 4 PRIORITY every
// 100 cycles around a VERB keypress to see what's happening to KEYRUPT1
// dispatch.

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

    printf("Before VERB: IR5=%d ch015=%02o slot4_PRIO=%05o slot0_PRIO=%05o\n",
           st->InterruptRequests[5],
           st->InputChannel[015] & 037,
           st->Erasable[0][0154 + 4*014 + 11] & 077777,
           st->Erasable[0][0154 + 0*014 + 11] & 077777);

    harness_post_key(DSKY_KEY_VERB);

    // Sample every 50 cycles for 5000 cycles. Report state transitions.
    int last_ir5 = -1, last_ch015 = -1, last_p4 = -1;
    for (long c = 0; c < 50000; c++) {
        agc_engine(st);
        if (c % 50 != 0) continue;
        int ir5 = st->InterruptRequests[5];
        int ch015 = st->InputChannel[015] & 037;
        int p4 = st->Erasable[0][0154 + 4*014 + 11] & 077777;
        if (ir5 != last_ir5 || ch015 != last_ch015 || p4 != last_p4) {
            int z = st->Erasable[0][5] & 07777;
            printf("  c=%5ld IR5=%d ch015=%02o slot4_PRIO=%05o Z=%05o\n",
                   c, ir5, ch015, p4, z);
            last_ir5 = ir5; last_ch015 = ch015; last_p4 = p4;
        }
    }
    PASS();
}
