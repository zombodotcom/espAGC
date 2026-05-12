// test_ref_compare — runs the same keystroke sequence as ref_capture.py
// (R, V36E, V37E00E, V37E00E) at WALL-CLOCK pacing matching yaAGC's
// SimExecute. Async pthread posts keypresses from a separate thread so
// they arrive at non-deterministic engine cycles, matching real-time
// socket-input behavior.
//
//   make test_ref_compare.exe
//   ROM=../../build/roms/Luminary099.bin ./test_ref_compare.exe > local.log
//   bash verify_ref_match.sh local.log golden/ref_V36_V37E00E_double_to_PRG00.log

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
}
#else
static void sleep_us(long us) {
    struct timespec ts = {us / 1000000, (us % 1000000) * 1000};
    nanosleep(&ts, NULL);
}
#endif

static void section(const char *label) {
    printf("%s\n", label); fflush(stdout);
}

#define MS              1000
#define KEY_GAP_US      (100 * MS)   // 0.1s between keys within a token
#define TOKEN_GAP_US    (3000 * MS)  // 3s settle after each token

struct keyplan { int code; int delay_us; };

static void *key_thread(void *arg) {
    struct keyplan *plan = (struct keyplan *)arg;
    for (int i = 0; plan[i].delay_us >= 0; i++) {
        if (plan[i].delay_us > 0) sleep_us(plan[i].delay_us);
        if (plan[i].code != 0) harness_post_key(plan[i].code);
    }
    return NULL;
}

int main(void) {
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
        { 0, 2000 * MS },                                                        // final 2s settle
        { 0, -1 },                                                               // terminator
    };
    pthread_t tid;
    pthread_create(&tid, NULL, key_thread, plan);

    // Total budget: ~25 sec wall-clock. harness_step_realtime now paces
    // at AGC_PER_SECOND = 85,333 cycles/sec (real Block II MCT rate,
    // matching upstream yaAGC's SimExecute). 25s * 85,333 ≈ 2.13M.
    // Key thread fires keys at wall-clock intervals (0.1s intra-token,
    // 3s inter-token); their arrival cycle alignment is asynchronous
    // to engine state, matching how yaAGC handles socket-input
    // keypresses in WSL.
    harness_step_realtime(85333 * 25);

    pthread_join(tid, NULL);

    section("--- DONE ---");
    return 0;
}
