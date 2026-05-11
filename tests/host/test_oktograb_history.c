// test_oktograb_history — keep a circular buffer of (bank, Z) pairs;
// when OKTOGRAB is reached, dump the last 60 unique entries to find
// who CALL'd INTSTALL.

#include "agc_harness.h"
#include "test_helpers.h"
#include "yaAGC.h"
#include "agc_engine.h"
#include <stdio.h>

extern agc_t *agc_core_state(void);
extern int agc_engine(agc_t *State);

static int cur_z(agc_t *s)  { return s->Erasable[0][RegZ] & 07777; }
static int cur_fb(agc_t *s) { return (s->Erasable[0][RegBB] & 076000) >> 10; }
static int cur_sb(agc_t *s) { return (s->OutputChannel7 & 0100) ? 1 : 0; }
static int eff_bank(agc_t *s) {
    int fb = cur_fb(s);
    return (fb >= 030 && cur_sb(s)) ? fb + 010 : fb;
}

#define HISTSZ 200
static int hist_bank[HISTSZ];
static int hist_z[HISTSZ];
static long long hist_cyc[HISTSZ];
static int hist_idx = 0;

int main(void)
{
    harness_boot();
    agc_t *s = agc_core_state();

    int dumped = 0;
    int last_b = -1, last_z = -1;

    for (long long i = 0; i < 1000000LL; i++) {
        int z = cur_z(s);
        int b = eff_bank(s);

        if (b != last_b || z != last_z) {
            hist_bank[hist_idx] = b;
            hist_z[hist_idx] = z;
            hist_cyc[hist_idx] = i;
            hist_idx = (hist_idx + 1) % HISTSZ;
            last_b = b;
            last_z = z;
        }

        if (b == 013 && z == 03460 && dumped == 0) {
            printf("=== OKTOGRAB at cyc=%lld - last %d unique addrs:\n",
                   i, HISTSZ);
            for (int k = 0; k < HISTSZ; k++) {
                int idx = (hist_idx + k) % HISTSZ;
                if (hist_cyc[idx] == 0 && idx != 0) continue;
                printf("  %3d: cyc=%-7lld %02o,%05o\n", k,
                       hist_cyc[idx], hist_bank[idx], hist_z[idx]);
            }
            dumped = 1;
            break;
        }

        agc_engine(s);
    }
    PASS();
}
