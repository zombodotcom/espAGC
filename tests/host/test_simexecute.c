// test_simexecute — drive the engine using the EXACT canonical SimExecute
// pattern (times()-paced 10ms ticks, 853-cycle bursts, SimSleep between).
//
// Difference from test_canonical_match: that test used microsecond pacing
// with 256-cycle batches. yaAGC.exe (which we just proved works on
// Windows mingw, 5/5 PRG=00) uses the SimExecute pattern from
// agc_simulator.c. If this test passes 5/5 too, the fix is "use the
// canonical SimExecute loop in our engine driver."
//
// Build: mingw32-make test_simexecute.exe
// Run:   ROM=../../build/roms/Luminary099.bin ./test_simexecute.exe

#include "yaAGC.h"
#include "agc_engine.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
static void sim_sleep_10ms(void) { Sleep(10); }
#else
static void sim_sleep_10ms(void) {
    struct timespec req = {0, 10000000};
    nanosleep(&req, NULL);
}
#endif

// times()-equivalent: return monotonic clock in CLK_TCK=100Hz ticks.
// Windows doesn't have times() so we synthesise 10ms-resolution ticks.
#define CLK_TCK 100
static long sim_times(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq = {0};
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return (long)((t.QuadPart * CLK_TCK) / freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)(ts.tv_sec * CLK_TCK + (ts.tv_nsec * CLK_TCK) / 1000000000L);
#endif
}

extern int agc_engine_init(agc_t *State, const char *RomImage,
                            const char *CoreDump, int AllOrErasable);

static agc_t state;
agc_t *agc_core_state(void) { return &state; }

// ---- driver thread (LM_INI + keys via shared volatiles) ---------------
#define MS 1000
static volatile int g_pending_key = -1;
static volatile int g_pending_lm_ini = 0;
static volatile int g_done = 0;
static volatile int g_emitted_55265 = 0;
static volatile long g_emit_cycle = 0;

static int ascii_to_key(char c) {
    switch (c) {
        case 'V': return 17;  case 'N': return 31;
        case '+': return 26;  case '-': return 27;
        case 'R': return 18;  case 'E': return 28;
        case 'C': return 30;  case 'P': return 25;
        case '0': return 16;
        case '1': return 1; case '2': return 2; case '3': return 3;
        case '4': return 4; case '5': return 5; case '6': return 6;
        case '7': return 7; case '8': return 8; case '9': return 9;
        default: return -1;
    }
}

#ifdef _WIN32
static void usleep_(long us) { if (us >= 1000) Sleep((DWORD)(us / 1000)); }
#else
static void usleep_(long us) {
    struct timespec ts = {us / 1000000, (us % 1000000) * 1000};
    nanosleep(&ts, NULL);
}
#endif

static void *driver(void *arg) {
    const char *seq = (const char *)arg;
    usleep_(500 * MS);
    g_pending_lm_ini = 1;
    while (g_pending_lm_ini && !g_done) usleep_(1000);
    usleep_(2000 * MS);
    const char *p = seq;
    while (*p && !g_done) {
        if (*p == ' ') { usleep_(3000 * MS); p++; continue; }
        int code = ascii_to_key(*p);
        if (code >= 0) {
            while (g_pending_key >= 0 && !g_done) usleep_(1000);
            g_pending_key = code;
            usleep_(100 * MS);
        }
        p++;
    }
    return NULL;
}

int main(int argc, char **argv) {
    const char *rom = getenv("ROM");
    if (!rom) rom = "../../build/roms/Luminary099.bin";
    const char *seq = getenv("SEQ");
    if (!seq) seq = "R V36E V37E 00E V37E 00E";

    int rc = agc_engine_init(&state, rom, NULL, 0);
    if (rc != 0) { fprintf(stderr, "agc_engine_init failed: %d\n", rc); return 1; }
    printf("test_simexecute: SimExecute-pattern host driver  seq='%s'\n", seq);

    pthread_t tid;
    pthread_create(&tid, NULL, driver, (void *)seq);

    // SimExecute pattern (agc_simulator.c:299-318):
    //   RealTimeOffset = times();  CycleCount initially = TCK*state.CycleCounter.
    //   loop: RealTime = times(); if changed, DesiredCycles = (RealTime - Offset) * AGC_PER_SECOND
    //         else SimSleep(10ms)
    //         while CycleCount < DesiredCycles: agc_engine(); CycleCount += CLK_TCK
    long real_time_offset = sim_times();
    long cycle_count = CLK_TCK * (long)state.CycleCounter;
    real_time_offset -= (cycle_count + AGC_PER_SECOND / 2) / AGC_PER_SECOND;
    long last_real_time = ~(long)0;
    long desired_cycles = 0;
    int  last_oc11 = -1;

    long deadline_ticks = sim_times() + 30 * CLK_TCK;   // 30s wall

    while (sim_times() < deadline_ticks) {
        // SimManageTime
        long now_t = sim_times();
        if (now_t != last_real_time) {
            last_real_time = now_t;
            desired_cycles = (now_t - real_time_offset) * AGC_PER_SECOND;
        } else {
            sim_sleep_10ms();
            continue;
        }
        // Apply any pending inputs before the burst — same place yaAGC's
        // SocketAPI ChannelInput would inject them.
        if (g_pending_lm_ini) {
            WriteIO(&state, 030, 036331);
            WriteIO(&state, 031, 077777);
            WriteIO(&state, 032, 022777);
            WriteIO(&state, 033, 057776);
            printf("[t=%ld ticks] LM_INI sent\n", now_t - real_time_offset);
            g_pending_lm_ini = 0;
        }
        int k = g_pending_key;
        if (k >= 0) {
            WriteIO(&state, 015, k & 037);
            state.InterruptRequests[5] = 1;
            printf("[t=%ld] key=%02o\n", now_t - real_time_offset, k & 037);
            g_pending_key = -1;
        }
        // 853-cycle burst.
        while (cycle_count < desired_cycles) {
            agc_engine(&state);
            cycle_count += CLK_TCK;
            int oc11 = state.OutputChannel10[11] & 077777;
            if (oc11 != last_oc11) {
                last_oc11 = oc11;
                if (oc11 == 055265 && !g_emitted_55265) {
                    g_emitted_55265 = 1;
                    g_emit_cycle = (long)state.CycleCounter;
                    printf("*** PRG=00 EMITTED at cyc=%ld ***\n", g_emit_cycle);
                }
            }
        }
    }
    g_done = 1;
    pthread_join(tid, NULL);
    printf("\nFINAL: cyc=%llu OC10[11]=%05o Z=%05o active_prio=%06o LOC=%06o\n",
           (unsigned long long)state.CycleCounter,
           state.OutputChannel10[11] & 077777,
           state.Erasable[0][5] & 077777,
           state.Erasable[0][0167] & 077777,
           state.Erasable[0][0164] & 077777);
    if (g_emitted_55265) {
        printf("RESULT: PRG=00 SUCCESS at cycle %ld\n", g_emit_cycle);
        return 0;
    }
    printf("RESULT: PRG=00 NOT REACHED\n");
    return 1;
}
