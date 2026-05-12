// test_state_compare — runs the same R V36E V37E 00E V37E 00E sequence as
// the WSL ground-truth capture (tests/host/capture_with_dumps.sh) and
// writes periodic core dumps in the SAME yaAGC ASCII-octal format. Pair
// the resulting dump dir with the WSL ref dir to diff state cell-by-cell.
//
//   make test_state_compare.exe
//   ROM=../../build/roms/Luminary099.bin \
//     DUMPDIR=wsl_dumps/host ./test_state_compare.exe
//
// Then:
//   python3 parse_core_dump.py wsl_dumps/host/core.NNN wsl_dumps/ref/core.NNN --diff
//
// Timing matches WSL: ~3s settle + 2s/token, ~24s total. Dumps every
// ~1 sec wall-clock. Each dump's CycleCounter is also written to
// dump_cycles.tsv for correlation.

#include "agc_harness.h"
#include "test_helpers.h"
#include "yaAGC.h"
#include "agc_engine.h"
extern agc_t *agc_core_state(void);

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
static void sleep_us(long us) { if (us >= 1000) Sleep((DWORD)(us / 1000)); }
static uint64_t now_us(void) {
    static LARGE_INTEGER freq = {0};
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return (uint64_t)((t.QuadPart * 1000000ULL) / freq.QuadPart);
}
static void make_dir(const char *p) { _mkdir(p); }
#else
static void sleep_us(long us) {
    struct timespec ts = {us / 1000000, (us % 1000000) * 1000};
    nanosleep(&ts, NULL);
}
static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
}
static void make_dir(const char *p) { mkdir(p, 0755); }
#endif

#define MS 1000

struct keyplan { int code; int delay_us; };

static struct keyplan plan[] = {
    { 0, 2500 * MS },                                                        // ini settle
    { 15, 0 }, { 0, 3000 * MS },                                             // R
    { 17, 0 }, { 3, 100 * MS }, { 6, 100 * MS }, { 28, 100 * MS }, { 0, 3000 * MS },  // V36E
    { 17, 0 }, { 3, 100 * MS }, { 7, 100 * MS }, { 28, 100 * MS }, { 0, 3000 * MS },  // V37E
    { 16, 0 }, { 16, 100 * MS }, { 28, 100 * MS }, { 0, 3000 * MS },                  // 00E
    { 17, 0 }, { 3, 100 * MS }, { 7, 100 * MS }, { 28, 100 * MS }, { 0, 3000 * MS },  // V37E
    { 16, 0 }, { 16, 100 * MS }, { 28, 100 * MS }, { 0, 2000 * MS },                  // 00E + tail
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

int main(void) {
    const char *dumpdir = getenv("DUMPDIR");
    if (!dumpdir || !*dumpdir) dumpdir = "wsl_dumps/host";

    // Best-effort path-component creation. tests/host is CWD already.
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", dumpdir);
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/' || *p == '\\') { *p = 0; make_dir(buf); *p = '/'; }
    }
    make_dir(dumpdir);

    char tsv_path[512];
    snprintf(tsv_path, sizeof(tsv_path), "%s/dump_cycles.tsv", dumpdir);
    FILE *tsv = fopen(tsv_path, "w");
    if (!tsv) { fprintf(stderr, "cannot create %s\n", tsv_path); return 1; }

    harness_boot();
    // Match WSL cold-boot: reset channels 030..033 to upstream defaults
    // (peripheral_stub_init pre-wrote LM_Sim values; WSL only applies
    // them after the Python client's socket writes land ~2s later).
    if (getenv("MATCH_WSL_BOOT")) {
        agc_t *s = agc_core_state();
        s->InputChannel[030] = 037777;
        s->InputChannel[031] = 077777;
        s->InputChannel[032] = 077777;
        s->InputChannel[033] = 077777;
        printf("[MATCH_WSL_BOOT] ch30..33 reset to upstream defaults\n");
    }
    printf("--- ini --- t=0.000\n"); fflush(stdout);

    pthread_t tid;
    pthread_create(&tid, NULL, key_thread, plan);

    // Run engine in 1-sec wall-clock slices, dumping at each boundary.
    // harness_step_realtime paces at AGC_PER_SECOND (85,333) cycles/sec
    // to match upstream yaAGC's SimExecute, so 85333 cycles ≈ 1 sec.
    const int slice_cycles = 85333;          // 1 sec wall-clock
    const int n_slices = 25;                 // ~25 sec total
    uint64_t t0 = now_us();
    for (int s = 0; s < n_slices; s++) {
        harness_step_realtime(slice_cycles);

        char path[600];
        snprintf(path, sizeof(path), "%s/core.%03d", dumpdir, s);
        if (harness_make_core_dump(path) != 0) {
            fprintf(stderr, "dump failed: %s\n", path);
        }
        unsigned long long cyc = harness_cycle_counter();
        double wall = (now_us() - t0) / 1e6;
        fprintf(tsv, "core.%03d\t%llo\t%.3f\n", s, cyc, wall);
        fflush(tsv);
        printf("dump core.%03d  cyc=%llo (%llu dec)  wall=%.3f\n",
               s, cyc, cyc, wall);
        fflush(stdout);
    }

    pthread_join(tid, NULL);
    fclose(tsv);
    printf("--- DONE ---\n");
    return 0;
}
