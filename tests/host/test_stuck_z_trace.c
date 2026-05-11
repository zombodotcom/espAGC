// test_stuck_z_trace — sample where the engine actually executes
// while slot 0 is "stuck" at PRIO=33002. Take 3000 instruction Z
// samples in a row and report unique Z values + visit counts.
//
// Goal: identify the actual loop (or chain of routines) by looking
// at *all* the addresses visited, not just a single snapshot point.

#include "agc_harness.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"

#include <stdio.h>
#include <stdlib.h>

extern agc_t *agc_core_state(void);
extern int agc_engine(agc_t *st);

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    for (long c = 0; c < 1000000; c++) agc_engine(st);

    static int counts[010000];
    int last_z = -1;
    for (long c = 0; c < 3000; c++) {
        agc_engine(st);
        int z = st->Erasable[0][5] & 07777;
        if (z != last_z) counts[z]++;
        last_z = z;
    }

    // Print all visited Z addresses, with their count.
    printf("Z addresses visited in 3000-cycle window after 1M boot:\n");
    int total = 0, distinct = 0;
    for (int z = 0; z < 010000; z++) {
        if (counts[z]) {
            distinct++;
            total += counts[z];
        }
    }
    printf("  distinct=%d, total instr=%d\n\n", distinct, total);

    // Sort and print top 40 most-visited.
    typedef struct { int z; int n; } p_t;
    p_t top[40] = {0};
    int ntop = 0;
    for (int z = 0; z < 010000; z++) {
        if (counts[z] == 0) continue;
        if (ntop < 40) { top[ntop].z=z; top[ntop].n=counts[z]; ntop++; }
        else {
            int imin = 0;
            for (int i = 1; i < ntop; i++) if (top[i].n < top[imin].n) imin = i;
            if (counts[z] > top[imin].n) { top[imin].z=z; top[imin].n=counts[z]; }
        }
    }
    for (int i = 0; i < ntop; i++)
        for (int j = i+1; j < ntop; j++)
            if (top[j].n > top[i].n) { p_t t=top[i]; top[i]=top[j]; top[j]=t; }
    printf("Top %d hottest Z values:\n", ntop);
    for (int i = 0; i < ntop; i++)
        printf("  %05o  visits=%d\n", top[i].z, top[i].n);

    PASS();
}
