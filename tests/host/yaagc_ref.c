// yaagc_ref — reference harness using UPSTREAM yaAGC's agc_engine_init.c
// directly (not our agc_init.c). Optional --sim flag interleaves our
// peripheral_stub_step (LM_Simulator port) every 1000 engine cycles.
//
// Use to compare:
//   ./yaagc_ref          → vanilla yaAGC + Luminary099, no peripherals
//   ./yaagc_ref --sim    → + our peripheral_stub fed periodically

#include "yaAGC.h"
#include "agc_engine.h"
#include "peripheral_stub.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int agc_engine_init(agc_t *State, const char *RomImage,
                            const char *CoreDump, int AllOrErasable);
extern int agc_engine(agc_t *State);

static agc_t state;
agc_t *agc_core_state(void) { return &state; }

int main(int argc, char **argv)
{
    const char *rom = NULL;
    int use_sim = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--sim")) use_sim = 1;
        else rom = argv[i];
    }
    if (!rom) rom = "../../build/roms/Luminary099.bin";

    int rc = agc_engine_init(&state, rom, NULL, 0);
    if (rc != 0) {
        fprintf(stderr, "agc_engine_init failed: rc=%d (rom=%s)\n", rc, rom);
        return 1;
    }
    if (use_sim) peripheral_stub_init();
    fprintf(stderr, "ROM loaded: %s  sim=%d\n", rom, use_sim);

    long checkpoints[] = {1000, 5000, 10000, 30000, 100000, 500000,
                           1000000, 2000000};
    int nc = sizeof(checkpoints) / sizeof(checkpoints[0]);
    long cur = 0;
    for (int i = 0; i < nc; i++) {
        while (cur < checkpoints[i]) {
            agc_engine(&state);
            cur++;
            if (use_sim && (cur % 1000) == 0)
                peripheral_stub_step(&state, 10000);  // 10ms simulated
        }
        int z   = state.Erasable[0][5]    & 07777;
        int fb  = (state.Erasable[0][4] >> 10) & 037;
        int p0  = state.Erasable[0][0167] & 077777;
        int polish = state.Erasable[0][0117] & 077777;
        int loc = state.Erasable[0][0164] & 077777;
        int redoctr = state.Erasable[0][0320] & 077777;
        int ch030 = state.InputChannel[030] & 077777;
        printf("@%7ld Z=%05o fb=%02o PRIO=%05o POLISH=%05o LOC=%05o REDOCTR=%05o ch030=%05o NW=%d TC=%d\n",
               cur, z, fb, p0, polish, loc, redoctr, ch030,
               state.NightWatchmanTripped, state.TCTrap);
    }

    // Histogram of last 10000 cycles' Z addresses. Stuck or progressing?
    int z_counts[010000] = {0};
    for (int c = 0; c < 10000; c++) {
        agc_engine(&state);
        if (use_sim && (c % 1000) == 0)
            peripheral_stub_step(&state, 10000);
        int z = state.Erasable[0][5] & 07777;
        z_counts[z]++;
    }
    int distinct = 0;
    for (int z = 0; z < 010000; z++) if (z_counts[z]) distinct++;
    typedef struct { int z; int n; } pair_t;
    pair_t top[8] = {0};
    int ntop = 0;
    for (int z = 0; z < 010000; z++) {
        if (!z_counts[z]) continue;
        if (ntop < 8) { top[ntop].z = z; top[ntop].n = z_counts[z]; ntop++; }
        else {
            int imin = 0;
            for (int i = 1; i < ntop; i++) if (top[i].n < top[imin].n) imin = i;
            if (z_counts[z] > top[imin].n) { top[imin].z = z; top[imin].n = z_counts[z]; }
        }
    }
    for (int i = 0; i < ntop; i++) {
        for (int j = i+1; j < ntop; j++)
            if (top[j].n > top[i].n) { pair_t t = top[i]; top[i]=top[j]; top[j]=t; }
    }
    printf("\nLast 10k cycles: %d distinct Z, top: ", distinct);
    for (int i = 0; i < ntop; i++) printf("%05o(%d) ", top[i].z, top[i].n);
    printf("\n");
    return 0;
}
