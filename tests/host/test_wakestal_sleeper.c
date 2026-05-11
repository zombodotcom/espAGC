// test_wakestal_sleeper — find which slot/job sleeps at WAKESTAL
// during the cold-boot warmup window.

#include "agc_harness.h"
#include "test_helpers.h"
#include "yaAGC.h"
#include "agc_engine.h"
#include <stdio.h>

extern agc_t *agc_core_state(void);
extern int agc_engine(agc_t *State);

#define WAKESTAL_CADR 027415

int main(void)
{
    harness_boot();
    agc_t *s = agc_core_state();

    int last_state[8] = {0};
    for (long long i = 0; i < 8000000LL; i++) {
        for (int slot = 0; slot < 8; slot++) {
            int base = 0154 + slot * 014;
            int loc  = s->Erasable[0][base + 8]  & 077777;
            int prio = s->Erasable[0][base + 11] & 077777;
            int sleeping = (prio & 040000) != 0;
            int state = (sleeping && loc == WAKESTAL_CADR);
            if (state != last_state[slot]) {
                printf("cyc=%-8lld slot%d sleeping_at_WAKESTAL %d->%d "
                       "prio=%05o loc=%05o\n",
                       i, slot, last_state[slot], state, prio, loc);
                last_state[slot] = state;
            }
        }
        agc_engine(s);
    }
    PASS();
}
