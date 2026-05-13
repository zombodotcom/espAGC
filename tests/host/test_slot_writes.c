// test_slot_writes — run V37E00E*2 like test_state_compare, but step the
// engine one instruction at a time and log every write to slot-0 cells
// E[0][0164] LOC, E[0][0165] BANKSET-low (actually MODE? but adjacent),
// E[0][0166] BBCON, E[0][0167] PRIORITY. Identifies exactly which Z
// addresses execute the writes that produce the crash signature
// (PRIO=030401, LOC=02146, BBCON=00401).
//
// Build:   mingw32-make test_slot_writes.exe
// Run:     ROM=../../build/roms/Luminary099.bin ./test_slot_writes.exe
// Output:  slot_writes.log

#include "agc_harness.h"
#include "test_helpers.h"
#include "yaAGC.h"
#include "agc_engine.h"
extern agc_t *agc_core_state(void);

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static void sleep_us(long us) { if (us >= 1000) Sleep((DWORD)(us / 1000)); }
static uint64_t now_us(void) {
    static LARGE_INTEGER freq = {0};
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return (uint64_t)((t.QuadPart * 1000000ULL) / freq.QuadPart);
}
#else
#include <time.h>
static void sleep_us(long us) {
    struct timespec ts = {us / 1000000, (us % 1000000) * 1000};
    nanosleep(&ts, NULL);
}
static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
}
#endif

#define MS 1000

struct keyplan { int code; int delay_us; };

// Same sequence as test_state_compare.c
static struct keyplan plan[] = {
    { 0, 2500 * MS },
    { 15, 0 }, { 0, 3000 * MS },
    { 17, 0 }, { 3, 100 * MS }, { 6, 100 * MS }, { 28, 100 * MS }, { 0, 3000 * MS },
    { 17, 0 }, { 3, 100 * MS }, { 7, 100 * MS }, { 28, 100 * MS }, { 0, 3000 * MS },
    { 16, 0 }, { 16, 100 * MS }, { 28, 100 * MS }, { 0, 3000 * MS },
    { 17, 0 }, { 3, 100 * MS }, { 7, 100 * MS }, { 28, 100 * MS }, { 0, 3000 * MS },
    { 16, 0 }, { 16, 100 * MS }, { 28, 100 * MS }, { 0, 2000 * MS },
    { 0, -1 },
};

static void *key_thread(void *arg) {
    struct keyplan *p = (struct keyplan *)arg;
    for (int i = 0; p[i].delay_us >= 0; i++) {
        if (p[i].delay_us > 0) sleep_us(p[i].delay_us);
        if (p[i].code != 0) harness_post_key(p[i].code);
    }
    return NULL;
}

// Watched cells: slot 0 of the executive's job table.
// Per ERASABLE_ASSIGNMENTS.agc:388-393, each slot is 12 cells starting at
// MPAC; LOC=MPAC+8, BANKSET=MPAC+9, PRIORITY=MPAC+11. Our parse_core_dump
// labels show MPAC slot 0 base = 0156: LOC@0164, ?@0165, BBCON@0166,
// PRIORITY@0167.
#define WATCH_LO 0164
#define WATCH_HI 0167

extern int agc_engine(agc_t *State);

int main(void) {
    const char *out_path = getenv("TRACE_FILE");
    if (!out_path || !*out_path) out_path = "slot_writes.log";
    FILE *trace = fopen(out_path, "w");
    if (!trace) { fprintf(stderr, "cannot create %s\n", out_path); return 1; }

    harness_boot();
    fprintf(trace, "# cyc Z_before FB EB A Q new[164] new[165] new[166] new[167]\n");
    fflush(trace);

    agc_t *s = agc_core_state();
    pthread_t tid;
    pthread_create(&tid, NULL, key_thread, plan);

    uint16_t prev[WATCH_HI - WATCH_LO + 1];
    for (int i = 0; i <= WATCH_HI - WATCH_LO; i++) prev[i] = s->Erasable[0][WATCH_LO + i];

    // Real-time pacing identical to harness_step_realtime, but with the
    // engine stepped one cycle at a time so we can watch for slot writes.
    const long total_cycles = 85333L * 26;     // ~26s of simulated time
    const uint64_t us_per_cycle_num = 1000000ULL;
    const uint64_t us_per_cycle_den = 85333ULL;
    const int peripheral_tick_every = 8191;
    uint64_t t0 = now_us();
    long elapsed = 0;
    long cycles_since_tick = 0;
    int writes_logged = 0;
    int z0_streak = 0;

    while (elapsed < total_cycles) {
        int z_before = s->Erasable[0][5] & 077777;
        int fb_before = (s->Erasable[0][4] >> 10) & 037;
        int eb_before = s->Erasable[0][3] & 07;
        int a_before = s->Erasable[0][0] & 077777;
        int q_before = s->Erasable[0][2] & 077777;

        agc_engine(s);
        elapsed++;
        cycles_since_tick++;

        int any_diff = 0;
        for (int i = 0; i <= WATCH_HI - WATCH_LO; i++) {
            uint16_t cur = s->Erasable[0][WATCH_LO + i];
            if (cur != prev[i]) { any_diff = 1; break; }
        }
        if (any_diff) {
            fprintf(trace,
                "cyc=%llo Z=%05o FB=%02o EB=%o A=%06o Q=%06o  "
                "[164]%05o->%05o [165]%05o->%05o [166]%05o->%05o [167]%05o->%05o\n",
                (unsigned long long)s->CycleCounter, z_before, fb_before, eb_before,
                a_before, q_before,
                prev[0], s->Erasable[0][0164],
                prev[1], s->Erasable[0][0165],
                prev[2], s->Erasable[0][0166],
                prev[3], s->Erasable[0][0167]);
            for (int i = 0; i <= WATCH_HI - WATCH_LO; i++)
                prev[i] = s->Erasable[0][WATCH_LO + i];
            writes_logged++;
            if (writes_logged % 100 == 0) fflush(trace);
        }

        // Detect crash: Z=0 with active slot occupied for many cycles.
        if ((s->Erasable[0][5] & 077777) == 0 &&
            (s->Erasable[0][0167] & 077777) != 077777) {
            z0_streak++;
            if (z0_streak == 1) {
                fprintf(trace,
                    "# CRASH DETECTED: cyc=%llo Z=0 PRIO=%06o LOC=%06o BBCON=%06o\n",
                    (unsigned long long)s->CycleCounter,
                    s->Erasable[0][0167] & 077777,
                    s->Erasable[0][0164] & 077777,
                    s->Erasable[0][0166] & 077777);
                fflush(trace);
            }
        } else {
            z0_streak = 0;
        }

        if (cycles_since_tick >= peripheral_tick_every) {
            // Call peripheral_stub_tick from agc_harness through harness_tick_peripherals
            harness_tick_peripherals();
            cycles_since_tick = 0;
        }

        // Pace every 256 cycles to avoid wasting time on now_us()
        if ((elapsed & 0xff) == 0) {
            uint64_t target = t0 +
                (uint64_t)elapsed * us_per_cycle_num / us_per_cycle_den;
            uint64_t now = now_us();
            while (now < target) {
                int64_t gap = (int64_t)(target - now);
                if (gap > 1500) sleep_us((long)gap);
                else break;
                now = now_us();
            }
        }
    }
    pthread_join(tid, NULL);

    fprintf(trace, "# END: %d slot writes logged, final Z=%05o PRIO=%06o\n",
            writes_logged, s->Erasable[0][5] & 077777, s->Erasable[0][0167] & 077777);
    fclose(trace);
    printf("wrote %d slot-write entries to %s\n", writes_logged, out_path);
    printf("final Z=%05o PRIO=%06o LOC=%06o BBCON=%06o\n",
           s->Erasable[0][5] & 077777,
           s->Erasable[0][0167] & 077777,
           s->Erasable[0][0164] & 077777,
           s->Erasable[0][0166] & 077777);
    return 0;
}
