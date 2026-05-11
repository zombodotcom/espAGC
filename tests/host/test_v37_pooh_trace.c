// test_v37_pooh_trace — log every Z address visited starting from
// V37 entry, capture first 800 unique steps.

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

    for (long i = 0; i < 200000; i++) agc_engine(s);
    harness_post_key(15);
    for (long i = 0; i < 50000; i++) agc_engine(s);
    harness_post_key(17); for (long i = 0; i < 50000; i++) agc_engine(s);
    harness_post_key(3);  for (long i = 0; i < 50000; i++) agc_engine(s);
    harness_post_key(7);  for (long i = 0; i < 50000; i++) agc_engine(s);
    harness_post_key(28); for (long i = 0; i < 50000; i++) agc_engine(s);
    harness_post_key(16); for (long i = 0; i < 50000; i++) agc_engine(s);
    harness_post_key(16); for (long i = 0; i < 50000; i++) agc_engine(s);
    harness_post_key(28); // final E

    int last_bank = -1, last_z = -1;
    int captured = 0;
    long long cyc = 0;
    int found_v37 = 0;

    for (long long i = 0; i < 50000000LL; i++) {
        int z = cur_z(s);
        int b = eff_bank(s);
        if (b == 04 && z == 02040) found_v37 = 1;
        if (b == 04 && z == 02223) {
            printf("  *** TS MODREG! cyc=%lld\n", cyc);
            break;
        }
        if (found_v37 && captured < 800 && (b != last_bank || z != last_z)) {
            printf("  step%4d cyc=%-8lld %02o,%05o\n", captured, cyc, b, z);
            captured++;
            last_bank = b;
            last_z = z;
        }
        agc_engine(s);
        cyc++;
    }
    printf("  end MODREG=%05o MMNUMBER=%05o\n",
           s->Erasable[2][011] & 077777,
           s->Erasable[1][0375] & 077777);
    PASS();
}
