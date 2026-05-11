// test_novac_dca — when an instruction at Z == 05100 (NOVAC's DCA 0)
// executes, capture A, L right after, plus Q, FBANK, BBANK, EBANK to
// see which NOVAC callers read zeros vs valid 2CADR words.
//
// Per test_charin_trace findings, some NOVAC calls produce slot LOC=0
// (the DCA read zeros) while others produce valid LOCs. The reads
// happen at the same engine PC (Z=05100, the DCA inside NOVAC) but
// presumably different FBANKs. This test logs every visit.

#include "agc_harness.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"

#include <stdio.h>

extern agc_t *agc_core_state(void);
extern int    agc_engine(agc_t *State);

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    int last_z = -1;
    int dca_hits = 0;

    printf("# cycle    Q     A_post L_post  FBANK BBANK EBANK isr  (caller's Q)\n");

    for (long cycle = 0; cycle < 1500000; cycle++) {
        agc_engine(st);
        if (cycle == 200000) {
            harness_post_key(/* VERB */ 17);
        }

        int z = st->Erasable[0][5];
        // DCA 0 in NOVAC is at fixed-fixed address 05100. After it runs,
        // Z advances to 05101 and A,L hold the read values.
        if (last_z == 05100 && z == 05101) {
            dca_hits++;
            int a   = st->Erasable[0][0] & 077777;
            int l   = st->Erasable[0][1] & 077777;
            int q   = st->Erasable[0][2] & 077777;
            int fb  = st->InputChannel[004] & 077777;
            int bb  = st->InputChannel[006] & 077777;
            int eb  = st->InputChannel[003] & 07;
            printf("%8ld  %05o  %05o  %05o  %05o %05o   %o    %d\n",
                   cycle, q, a, l, fb, bb, eb, st->InIsr);
            if (dca_hits > 30) {
                printf("... stopping at 30 hits\n");
                break;
            }
        }
        last_z = z;
    }

    printf("\nTotal DCA hits at Z=05100: %d\n", dca_hits);
    PASS();
}
