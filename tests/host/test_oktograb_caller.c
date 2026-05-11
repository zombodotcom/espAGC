// test_oktograb_caller — find who calls INTSTALL → OKTOGRAB at cold
// boot and never releases via INTWAKE. RegQ at OKTOGRAB entry holds
// the return address (the caller's L+1 from the CALL). For
// interpretive CALL it's the address of the next interpretive opcode
// after the target word.

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

int main(void)
{
    harness_boot();
    agc_t *s = agc_core_state();

    int oktograb_count = 0;
    int intwake_count = 0;
    int prev_rasflag = 0;

    for (long long i = 0; i < 5000000LL; i++) {
        int z = cur_z(s);
        int b = eff_bank(s);

        if (b == 013 && z == 03460 && oktograb_count < 8) {
            int q = s->Erasable[0][2] & 07777;
            int fb_raw = s->Erasable[0][RegFB] & 077777;
            int bb = s->Erasable[0][RegBB] & 077777;
            int rasflag = s->Erasable[0][0106] & 077777;
            int slot0_prio = s->Erasable[0][0167] & 077777;
            int slot0_loc  = s->Erasable[0][0164] & 077777;
            int newjob = s->Erasable[0][067] & 077777;
            printf("OKTOGRAB#%d cyc=%lld Q=%05o FB=%05o BB=%05o "
                   "RASFLAG_before=%05o slot0_prio=%05o slot0_loc=%05o newjob=%05o\n",
                   oktograb_count, i, q, fb_raw, bb, rasflag,
                   slot0_prio, slot0_loc, newjob);
            oktograb_count++;
        }

        if ((b == 013 && z == 03425) || (b == 013 && z == 03445)) {
            int rasflag = s->Erasable[0][0106] & 077777;
            printf("INTWAKE-entry cyc=%lld z=%02o,%05o RASFLAG=%05o\n",
                   i, b, z, rasflag);
            intwake_count++;
        }

        // Detect RASFLAG transitions
        int rasflag = s->Erasable[0][0106] & 077777;
        if ((rasflag & 020000) != (prev_rasflag & 020000)) {
            printf("RASFLAG bit14 %d->%d at cyc=%lld z=%02o,%05o\n",
                   (prev_rasflag & 020000) ? 1 : 0,
                   (rasflag & 020000) ? 1 : 0,
                   i, b, z);
        }
        if ((rasflag & 0100) != (prev_rasflag & 0100)) {
            printf("RASFLAG bit7 %d->%d at cyc=%lld z=%02o,%05o\n",
                   (prev_rasflag & 0100) ? 1 : 0,
                   (rasflag & 0100) ? 1 : 0,
                   i, b, z);
        }
        prev_rasflag = rasflag;

        agc_engine(s);
    }

    int rasflag = s->Erasable[0][0106] & 077777;
    printf("\nfinal RASFLAG=%05o oktograb=%d intwake=%d\n",
           rasflag, oktograb_count, intwake_count);
    PASS();
}
