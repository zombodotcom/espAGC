// test_v37_final_crash — capture engine state cycle-by-cycle around
// the second V37+0+0+ENTR to find what specific instruction corrupts
// slot 0 BANKSET / drives Z=0.
//
// Uses real-time pacing via pthread async keypresses.

#include "agc_harness.h"
#include "test_helpers.h"
#include "yaAGC.h"
#include "agc_engine.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
static void sleep_us(long us) {
    if (us >= 1000) Sleep((DWORD)(us / 1000));
    else { volatile int i; for (i = 0; i < 100; i++); }
}
#else
static void sleep_us(long us) {
    struct timespec ts = {us / 1000000, (us % 1000000) * 1000};
    nanosleep(&ts, NULL);
}
#endif

extern agc_t *agc_core_state(void);

#define MS 1000
#define KEY_GAP_US   (100 * MS)
#define TOKEN_GAP_US (3000 * MS)

struct keyplan { int code; int delay_us; };

static volatile int g_final_e_posted = 0;

static void *key_thread(void *arg) {
    struct keyplan *plan = (struct keyplan *)arg;
    int e_count = 0;
    for (int i = 0; plan[i].delay_us >= 0; i++) {
        if (plan[i].delay_us > 0) sleep_us(plan[i].delay_us);
        if (plan[i].code != 0) {
            harness_post_key(plan[i].code);
            if (plan[i].code == 28) {
                e_count++;
                if (e_count == 6) {
                    // 6th ENTR = the final one
                    g_final_e_posted = 1;
                    fprintf(stderr, "*** final ENTR posted ***\n");
                }
            }
        }
    }
    return NULL;
}

static void dump(const char *l) {
    agc_t *s = agc_core_state();
    int z = s->Erasable[0][5] & 077777;
    int active_prio = s->Erasable[0][0167] & 077777;
    int modreg = s->Erasable[2][0011] & 077777;
    int mmnumber = s->Erasable[1][0375] & 077777;
    fprintf(stderr, "[%-20s] cyc=%9lld Z=%05o prio0=%06o MODREG=%06o MM#=%06o\n",
            l, (long long)s->CycleCounter, z, active_prio, modreg, mmnumber);
    fprintf(stderr, "  slot 0: ");
    for (int i = 0; i < 12; i++) fprintf(stderr, "%05o ", s->Erasable[0][0154+i] & 077777);
    fprintf(stderr, "\n");
}

int main(void) {
    harness_boot();
    fprintf(stderr, "boot complete\n");

    static struct keyplan plan[] = {
        { 0, 2500 * MS },
        { 15, 0 }, { 0, TOKEN_GAP_US },
        { 17, 0 }, { 3, KEY_GAP_US }, { 6, KEY_GAP_US }, { 28, KEY_GAP_US },
        { 0, TOKEN_GAP_US },
        { 17, 0 }, { 3, KEY_GAP_US }, { 7, KEY_GAP_US }, { 28, KEY_GAP_US },
        { 0, TOKEN_GAP_US },
        { 16, 0 }, { 16, KEY_GAP_US }, { 28, KEY_GAP_US },
        { 0, TOKEN_GAP_US },
        { 17, 0 }, { 3, KEY_GAP_US }, { 7, KEY_GAP_US }, { 28, KEY_GAP_US },
        { 0, TOKEN_GAP_US },
        { 16, 0 }, { 16, KEY_GAP_US }, { 28, KEY_GAP_US },
        { 0, 2000 * MS },
        { 0, -1 },
    };
    pthread_t tid;
    pthread_create(&tid, NULL, key_thread, plan);

    // Run cycles paced to 1 MHz. Dump state on Z transition near crash.
    long total_cycles = 18000000;
    long batch = 100000;
    long elapsed = 0;
    int prev_z = 1;
    int sample = 0;
    while (elapsed < total_cycles) {
        long this_batch = batch;
        if (elapsed + this_batch > total_cycles) this_batch = total_cycles - elapsed;
        harness_step((int)this_batch);
        elapsed += this_batch;

        agc_t *s = agc_core_state();
        int z = s->Erasable[0][5] & 077777;
        int prio0 = s->Erasable[0][0167] & 077777;
        sample++;

        // Log first time Z becomes 0 and surrounding state
        if (prev_z != 0 && z == 0) {
            char lab[32]; sprintf(lab, "Z=0 enter samp%d", sample);
            dump(lab);
        }
        if (prio0 == 030110) {
            static int charin_seen = 0;
            if (!charin_seen) {
                char lab[32]; sprintf(lab, "CHARIN samp%d", sample);
                dump(lab);
                charin_seen = 1;
            }
        }
        prev_z = z;
        sleep_us(100 * 1000);
    }
    pthread_join(tid, NULL);
    dump("end");
    return 0;
}
