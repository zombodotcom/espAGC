// test_ref_compare — runs the same keystroke sequence as ref_capture.py
// (RSET, V35E, V37E00E) and emits raw output channel writes in the same
// format. Run with:
//
//   make test_ref_compare.exe
//   ROM=../../build/roms/Luminary099.bin ./test_ref_compare.exe > local.log
//   diff -u golden/ref_V35E_V37E00E.log local.log | head
//
// Build with -DCONFIG_AGC_LOG_ALL_OUTPUTS=1 (Makefile takes care of this
// for this test).

#include "agc_harness.h"
#include "test_helpers.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
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

static void section(const char *label) { printf("%s\n", label); fflush(stdout); }

// Async-keypress thread: matches WSL ref_capture.py behavior where the
// Python client posts keys at wall-clock intervals while yaAGC runs
// real-time-paced cycles. Posting keys before harness_step (the prior
// approach) makes KEYRUPT1 fire at deterministic cycle 0 of the next
// batch — which crashes the engine on the second V37+ENTR. Posting
// keys from a separate thread at wall-clock intervals replicates the
// real-time interleaving and (we hope) breaks the crash alignment.

struct keyplan {
    int code;       // DSKY keycode (0 means end-of-list)
    int delay_us;   // microseconds to wait before posting this key
};

static void *key_thread(void *arg) {
    struct keyplan *plan = (struct keyplan *)arg;
    for (int i = 0; plan[i].delay_us >= 0; i++) {
        if (plan[i].delay_us > 0) sleep_us(plan[i].delay_us);
        if (plan[i].code != 0) {
            harness_post_key(plan[i].code);
            fprintf(stderr, "key posted: %d\n", plan[i].code);
        }
    }
    return NULL;
}

static void key(int code, int step_cycles) {
    harness_post_key(code); harness_step(step_cycles);
}

// Match ref_capture.py wall-clock pacing at the 1 MHz nominal rate
// yaAGC runs in WSL: 0.1 sec between keys (=100K cycles), 3 sec post-ENTR
// settle (=3M cycles).
#define INTRA_KEY_CYCLES  100000
#define POST_ENTR_CYCLES  3000000

// Match ref_capture.py timing exactly:
//   - 0.1 sec between keys within a token
//   - 3 sec settle after each token
// All keys posted from a separate thread; main thread runs cycles
// continuously at ~1MHz (= ~26 sec total wall-clock for the test).
//
// Total wall-clock time = (5 tokens × 3 sec) + 0.1s × ~15 keys + 0.5s
//                       ≈ 17 sec.
// Cycle count to run = 17 sec × 1024000 cps ≈ 17.4M cycles.

#define MS  1000        // microseconds in 1 ms
#define KEY_GAP_US      (100 * MS)   // 0.1 sec between keys
#define TOKEN_GAP_US    (3000 * MS)  // 3 sec settle after each token

int main(void)
{
    harness_boot();
    section("--- ini ---");

    // Loop terminator is {0, -1}. {0, N>0} = sleep N us. {C>0, N} =
    // sleep N then post key C.
    static struct keyplan plan[] = {
        { 0, 2500 * MS },                                                        // initial 2.5s settle
        { 15, 0 }, { 0, TOKEN_GAP_US },                                          // R
        { 17, 0 }, { 3, KEY_GAP_US }, { 6, KEY_GAP_US }, { 28, KEY_GAP_US },
        { 0, TOKEN_GAP_US },                                                     // V36E
        { 17, 0 }, { 3, KEY_GAP_US }, { 7, KEY_GAP_US }, { 28, KEY_GAP_US },
        { 0, TOKEN_GAP_US },                                                     // V37E
        { 16, 0 }, { 16, KEY_GAP_US }, { 28, KEY_GAP_US },
        { 0, TOKEN_GAP_US },                                                     // 00E
        { 17, 0 }, { 3, KEY_GAP_US }, { 7, KEY_GAP_US }, { 28, KEY_GAP_US },
        { 0, TOKEN_GAP_US },                                                     // V37E
        { 16, 0 }, { 16, KEY_GAP_US }, { 28, KEY_GAP_US },
        { 0, TOKEN_GAP_US },                                                     // 00E
        { 0, 2000 * MS },                                                        // final settle
        { 0, -1 },                                                               // terminator
    };
    pthread_t tid;
    pthread_create(&tid, NULL, key_thread, plan);

    // Pace main loop to ~1 MHz wall-clock so the key thread's
    // microsecond-level delays align with simulated cycle time. At
    // 100k cycles per 100ms batch, total ~18 sec wall-clock = ~18M
    // cycles simulated. This matches yaAGC's real-time pacing.
    long total_cycles = 18000000;
    long batch = 100000;
    long elapsed = 0;
    while (elapsed < total_cycles) {
        long this_batch = batch;
        if (elapsed + this_batch > total_cycles) this_batch = total_cycles - elapsed;
        harness_step((int)this_batch);
        elapsed += this_batch;
        // Pace: sleep enough to give the key thread time to wake up at
        // its wall-clock delay points. 100k cycles at 1 MHz = 100ms.
        sleep_us(100 * 1000);  // 100 ms
    }
    pthread_join(tid, NULL);

    section("--- DONE ---");
    return 0;
}
