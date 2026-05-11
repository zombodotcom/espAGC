// test_v37_zhist — after V37E, log the most-frequented Z addresses
// during the 200k cycles right after the E keystroke. Tells us what
// the engine is running — is it spinning in CHARIN, in interpretive
// code, or actually getting through VERBFAN to MMCHANG?

#include "agc_harness.h"
#include "test_helpers.h"
#include "yaAGC.h"
#include "agc_engine.h"
#include <stdio.h>
#include <stdlib.h>

extern agc_t *agc_core_state(void);
extern int agc_engine(agc_t *State);

static int cur_z(agc_t *s) { return s->Erasable[0][RegZ] & 07777; }
static int cur_fb(agc_t *s) { return (s->Erasable[0][RegBB] & 076000) >> 10; }
static int cur_eb(agc_t *s) { return (s->Erasable[0][RegBB] & 07) << 8; }

#define BUCKETS 4096
static long hist[BUCKETS];      // Z address (0-07777)
static long fb_hist[64];        // FB bank (0-63)

static int compare_long_desc(const void *a, const void *b)
{
    long la = *(const long *)a, lb = *(const long *)b;
    return (lb > la) - (lb < la);
}

static void log_top(const char *tag)
{
    long top[20];
    int  idx[20];
    for (int i = 0; i < 20; i++) { top[i] = 0; idx[i] = -1; }
    for (int z = 0; z < BUCKETS; z++) {
        long v = hist[z];
        if (v == 0) continue;
        for (int i = 0; i < 20; i++) {
            if (v > top[i]) {
                for (int j = 19; j > i; j--) { top[j] = top[j-1]; idx[j] = idx[j-1]; }
                top[i] = v;
                idx[i] = z;
                break;
            }
        }
    }
    printf("--- top Z (%s) ---\n", tag);
    for (int i = 0; i < 20 && idx[i] >= 0; i++) {
        printf("  Z=%05o count=%ld\n", idx[i], top[i]);
    }
    printf("--- FB bank histogram (%s) ---\n", tag);
    for (int b = 0; b < 64; b++) {
        if (fb_hist[b] > 0) printf("  FB=%02o count=%ld\n", b, fb_hist[b]);
    }
}

static void clear_hist(void) {
    for (int i = 0; i < BUCKETS; i++) hist[i] = 0;
    for (int i = 0; i < 64; i++) fb_hist[i] = 0;
}

static void run_and_hist(agc_t *s, long n_cycles)
{
    for (long i = 0; i < n_cycles; i++) {
        int z = cur_z(s);
        int fb = cur_fb(s);
        if (z < BUCKETS) hist[z]++;
        if (fb < 64) fb_hist[fb]++;
        agc_engine(s);
    }
}

int main(void)
{
    harness_boot();
    harness_step(200000);
    harness_type("R", 50000);

    agc_t *s = agc_core_state();

    // V
    harness_post_key(17);
    clear_hist();
    run_and_hist(s, 200000);
    log_top("after V");

    // 3
    harness_post_key(3);
    clear_hist();
    run_and_hist(s, 200000);
    log_top("after 3");

    // 7
    harness_post_key(7);
    clear_hist();
    run_and_hist(s, 200000);
    log_top("after 7");

    // E
    harness_post_key(28);
    clear_hist();
    run_and_hist(s, 200000);
    log_top("after E (V37E)");

    PASS();
}
