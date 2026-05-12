// agc_harness.c — implementation. Pulls in the real channel_router and
// agc_core; the FreeRTOS+esp_log shims are in tests/host/include.

#include "agc_harness.h"
#include "test_helpers.h"

#include "agc_core.h"
#include "channel_router.h"
#include "dsky_keys.h"
#include "peripheral_stub.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static inline uint64_t now_us(void) {
    static LARGE_INTEGER freq = {0};
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return (uint64_t)((t.QuadPart * 1000000ULL) / freq.QuadPart);
}
#else
#include <time.h>
static inline uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
}
#endif

// Pull in the engine state struct so we can read alarm fields.
// __embedded__ is set on the command line (-D__embedded__).
#include "yaAGC.h"
#include "agc_engine.h"

extern agc_t *agc_core_state(void);
extern int InhibitAlarms;

void harness_boot(void)
{
    size_t sz;
    uint8_t *rom = load_rom_file(&sz);
    channel_router_init();
    int rc = agc_core_init(rom, sz);
    if (rc != 0) { fprintf(stderr, "agc_core_init -> %d\n", rc); exit(1); }
    peripheral_stub_init();
    // Suppress yaAGC's TC-Trap / NightWatchman / RuptLock GOJAM-on-alarm
    // behavior. These alarms fire on the host build during normal boot
    // (~6 TCTrap + 2 NW per 1M cycles even with no peripheral activity)
    // and wipe slot allocations before CHARIN can run for a keypress.
    // Pi/Linux yaAGC builds also expose this as the `--inhibit-alarms`
    // command-line option; we set it programmatically since we don't
    // have a CLI. Alarm latches still appear in ch77 for observation.
    InhibitAlarms = 1;
    free(rom);
}

static int g_peripheral_tick_interval = 0;
static long g_cycles_since_tick = 0;

void harness_set_peripheral_tick_interval(int cycles)
{
    g_peripheral_tick_interval = cycles;
    g_cycles_since_tick = 0;
}

void harness_tick_peripherals(void)
{
    peripheral_stub_tick(agc_core_state());
}

void harness_step(int n_cycles)
{
    if (n_cycles <= 0) return;
    if (g_peripheral_tick_interval <= 0) {
        agc_core_step(n_cycles);
        return;
    }
    // Run in chunks, calling peripheral_stub_tick between chunks.
    int remaining = n_cycles;
    while (remaining > 0) {
        int chunk = g_peripheral_tick_interval - (int)g_cycles_since_tick;
        if (chunk <= 0 || chunk > remaining) chunk = remaining;
        agc_core_step(chunk);
        g_cycles_since_tick += chunk;
        remaining -= chunk;
        if (g_cycles_since_tick >= g_peripheral_tick_interval) {
            peripheral_stub_tick(agc_core_state());
            g_cycles_since_tick = 0;
        }
    }
}

void harness_post_key(int code)
{
    channel_router_post_key(code);
}

// Real-time-paced step. Matches yaAGC's SimExecute behavior on Linux:
// catch up to wall-clock at 1 MHz. Runs cycles in small batches and
// busy-waits between batches to maintain the target rate.
//
// The pacing keeps T3/T4/T5RUPT timer firings at their wall-clock
// alignment relative to async keypresses arriving from another thread.
// Back-to-back execution puts firings at deterministic-but-pathological
// offsets that crash Luminary's bank-switching paths (V37+ENTR → NEWMODE).
void harness_step_realtime(int n_cycles)
{
    if (n_cycles <= 0) return;
    // Match yaAGC's SimExecute pacing on Linux: times() has 10ms
    // granularity, so the engine runs ~10240 cycles in a burst when
    // wall-time has advanced one CLK_TCK tick. We mimic that here.
    const int batch = 10240;
    uint64_t t0 = now_us();
    long elapsed_cycles = 0;
    while (elapsed_cycles < n_cycles) {
        int this_batch = batch;
        if (elapsed_cycles + this_batch > n_cycles)
            this_batch = n_cycles - (int)elapsed_cycles;
        if (g_peripheral_tick_interval > 0) {
            harness_step(this_batch);     // routes peripheral_stub_tick
        } else {
            agc_core_step(this_batch);
        }
        elapsed_cycles += this_batch;
        // Target wall-clock for this batch: t0 + elapsed_cycles us
        uint64_t target = t0 + (uint64_t)elapsed_cycles;
        uint64_t now = now_us();
        while (now < target) {
            int64_t gap = (int64_t)(target - now);
            if (gap > 1500) {
#ifdef _WIN32
                Sleep((DWORD)(gap / 1000));
#else
                struct timespec ts = {gap / 1000000, (gap % 1000000) * 1000};
                nanosleep(&ts, NULL);
#endif
            }
            now = now_us();
        }
    }
}

void harness_snapshot(dsky_state_t *out)
{
    channel_router_snapshot(out);
}

void harness_alarms(harness_alarms_t *out)
{
    agc_t *s = agc_core_state();
    out->rupt_lock              = s->RuptLock              != 0;
    out->night_watchman_tripped = s->NightWatchmanTripped  != 0;
    out->tc_trap                = s->TCTrap                != 0;
    out->no_tc                  = s->NoTC                  != 0;
    out->parity_fail            = s->ParityFail            != 0;
    out->warning_filter_active  = s->WarningFilter         != 0;
    out->generated_warning      = s->GeneratedWarning      != 0;
}

void harness_failreg(harness_failreg_t *out)
{
    agc_t *s = agc_core_state();
    out->latest = s->Erasable[0][0375];
    out->second = s->Erasable[0][0376];
    out->third  = s->Erasable[0][0377];
}

static int parse_one(char c)
{
    c = (char)toupper((unsigned char)c);
    if (c >= '0' && c <= '9') return c == '0' ? DSKY_KEY_0 : (c - '0');
    switch (c) {
        case '+': return DSKY_KEY_PLUS;
        case '-': return DSKY_KEY_MINUS;
        case 'E': return DSKY_KEY_ENTR;
        case 'V': return DSKY_KEY_VERB;
        case 'N': return DSKY_KEY_NOUN;
        case 'C': return DSKY_KEY_CLR;
        case 'P': return DSKY_KEY_PRO;
        case 'R': return DSKY_KEY_RSET;
        case 'K': return DSKY_KEY_KEYREL;
        default:  return -1;
    }
}

void harness_type(const char *seq, int gap_cycles)
{
    for (; *seq; seq++) {
        if (isspace((unsigned char)*seq)) continue;
        int code = parse_one(*seq);
        if (code < 0) { fprintf(stderr, "harness_type: bad token '%c'\n", *seq); exit(1); }
        harness_post_key(code);
        harness_step(gap_cycles);
    }
}
