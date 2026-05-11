// test_v37_bankhist — histogram of Z addresses *per bank* during the
// 200k cycles after the final E of V37E00E. Helps locate which bank
// the engine is executing in.

#include "agc_harness.h"
#include "test_helpers.h"
#include "yaAGC.h"
#include "agc_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern agc_t *agc_core_state(void);
extern int agc_engine(agc_t *State);

static int cur_z(agc_t *s)  { return s->Erasable[0][RegZ] & 07777; }
static int cur_fb(agc_t *s) { return (s->Erasable[0][RegBB] & 076000) >> 10; }
static int cur_superbnk(agc_t *s) { return (s->OutputChannel7 & 0100) ? 1 : 0; }

// Effective bank (matches yaYUL listing convention).
static int eff_bank(agc_t *s)
{
    int fb = cur_fb(s);
    int sb = cur_superbnk(s);
    if (fb >= 030 && sb) return fb + 010;
    return fb;
}

#define MAX_BANKS 050  // 040 octal banks
static long bank_z_hist[MAX_BANKS][04000];
static long bank_total[MAX_BANKS];

static void clear_hist(void) {
    memset(bank_z_hist, 0, sizeof(bank_z_hist));
    memset(bank_total, 0, sizeof(bank_total));
}

static void log_top_per_bank(const char *tag, int bank)
{
    if (bank_total[bank] == 0) return;
    long top[15];
    int  idx[15];
    for (int i = 0; i < 15; i++) { top[i] = 0; idx[i] = -1; }
    for (int z = 0; z < 04000; z++) {
        long v = bank_z_hist[bank][z];
        if (v == 0) continue;
        for (int i = 0; i < 15; i++) {
            if (v > top[i]) {
                for (int j = 14; j > i; j--) { top[j] = top[j-1]; idx[j] = idx[j-1]; }
                top[i] = v;
                idx[i] = z;
                break;
            }
        }
    }
    printf("--- bank %02o, %s (total=%ld) ---\n", bank, tag, bank_total[bank]);
    for (int i = 0; i < 15 && idx[i] >= 0; i++) {
        // Adjust Z to listing convention (banks 04+ at 02000-03777)
        int z_disp = (bank >= 04) ? (idx[i] | 02000) : idx[i];
        printf("  %02o,%05o count=%ld\n", bank, z_disp, top[i]);
    }
}

static void run_and_hist(agc_t *s, long n_cycles)
{
    for (long i = 0; i < n_cycles; i++) {
        int z = cur_z(s);
        int bank = eff_bank(s);
        // For fixed-switchable, Z is 02000-03777, so we use z & 01777 as index.
        // For fixed-fixed banks 02/03, Z is 04000-07777.
        int z_idx = (bank >= 04) ? (z & 01777) : (z & 03777);
        if (bank < MAX_BANKS && z_idx < 04000) {
            bank_z_hist[bank][z_idx]++;
            bank_total[bank]++;
        }
        agc_engine(s);
    }
}

int main(void)
{
    harness_boot();
    harness_step(200000);
    harness_type("R", 50000);
    harness_type("V37E", 200000);

    agc_t *s = agc_core_state();

    clear_hist();
    run_and_hist(s, 200000);
    printf("=== after V37E, 200k cycles ===\n");
    for (int b = 0; b < MAX_BANKS; b++) log_top_per_bank("after V37E", b);

    harness_post_key(16); harness_step(200000);
    clear_hist();
    harness_post_key(16); harness_step(200000);
    clear_hist();
    run_and_hist(s, 200000);
    printf("=== after V37E00, 200k cycles ===\n");
    for (int b = 0; b < MAX_BANKS; b++) log_top_per_bank("after V37E00", b);

    harness_post_key(28);
    clear_hist();
    run_and_hist(s, 200000);
    printf("=== after V37E00E, 200k cycles ===\n");
    for (int b = 0; b < MAX_BANKS; b++) log_top_per_bank("after V37E00E", b);

    PASS();
}
