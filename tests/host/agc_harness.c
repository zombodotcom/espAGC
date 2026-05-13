// agc_harness.c — implementation. Pulls in the real channel_router and
// agc_core; the FreeRTOS+esp_log shims are in tests/host/include.

#include "agc_harness.h"
#include "test_helpers.h"

#include "agc_core.h"
#include "channel_router.h"
#include "dsky_keys.h"
#include "peripheral_stub.h"
#include "yaagc_socket.h"

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

void harness_boot(void)
{
    size_t sz;
    uint8_t *rom = load_rom_file(&sz);
    channel_router_init();
    int rc = agc_core_init(rom, sz);
    if (rc != 0) { fprintf(stderr, "agc_core_init -> %d\n", rc); exit(1); }
    // Bring up the canonical SocketAPI synthetic-client slot before any
    // peripheral_stub LM_INI runs. Port 0 binds an ephemeral OS-picked
    // listener — fine for unit tests, nothing connects to it.
    if (yaagc_socket_init(0) != 0) {
        fprintf(stderr, "yaagc_socket_init(0) failed\n"); exit(1);
    }
    peripheral_stub_init();
    // No alarm-inhibit: upstream yaAGC's natural cold-boot recovery
    // depends on NW / TC-Trap firing during STARTSUB, triggering GOJAM,
    // and looping through FRESH_START until the engine reaches PINBALL.
    // See third_party/virtualagc/yaAGC/agc_engine.c:2242. Earlier we set
    // InhibitAlarms=1 because alarm rate was 12x too high (pacing was
    // 1 MHz instead of AGC_PER_SECOND=85,333). With harness_step_realtime
    // now pacing correctly, alarms fire at canonical rate.
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

// Real-time-paced step. Matches upstream yaAGC's SimExecute pacing:
// agc_engine() is called at AGC_PER_SECOND (85,333) calls per real
// second — the Block II AGC's actual MCT rate. Earlier versions of
// this function paced at 1 MHz (1us per cycle), which was 12x too
// fast and caused our engine to over-execute between async keypress
// arrivals, missing the timer-rupt / CHARIN-dispatch alignment that
// V37E00E relies on. Verified by comparing CycleCounter against WSL
// yaAGC core dumps: ours hit 19M cycles in 19s wall-clock vs WSL's
// 1.7M in the same window before the fix.
void harness_step_realtime(int n_cycles)
{
    if (n_cycles <= 0) return;
    // yaAGC's SimExecute on Linux uses times() with CLK_TCK=100 (10ms
    // ticks) and runs AGC_PER_SECOND/CLK_TCK ≈ 853 cycles per tick.
    // We use a slightly smaller batch for finer pacing on Windows.
    const int batch = 256;
    // 1e6 us / AGC_PER_SECOND = ~11.72 us per cycle.
    const uint64_t us_per_cycle_num = 1000000ULL;
    const uint64_t us_per_cycle_den = 85333ULL;  // AGC_PER_SECOND
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
        uint64_t target = t0 +
            (uint64_t)elapsed_cycles * us_per_cycle_num / us_per_cycle_den;
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

unsigned long long harness_cycle_counter(void)
{
    return (unsigned long long)agc_core_state()->CycleCounter;
}

// Mirror agc_engine_init.c:MakeCoreDump exactly so the output is
// byte-identical to yaAGC's --dump-time core dumps. Layout:
//   512 InputChannel  |  8x256 Erasable  |  CycleCounter
//   ExtraCode, AllowInterrupt, PendFlag, PendDelay, ExtraDelay
//   OutputChannel7  |  16x OutputChannel10  |  IndexValue
//   11x InterruptRequests  |  InIsr, SubstituteInstruction
//   DownruptTimeValid, DownruptTime, Downlink
int harness_make_core_dump(const char *path)
{
    agc_t *s = agc_core_state();
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;

    for (int i = 0; i < NUM_CHANNELS; i++)
        fprintf(fp, "%06o\n", (unsigned)(s->InputChannel[i] & 0xFFFF));

    for (int bank = 0; bank < 8; bank++)
        for (int j = 0; j < 0400; j++)
            fprintf(fp, "%06o\n", (unsigned)(s->Erasable[bank][j] & 0xFFFF));

    fprintf(fp, "%llo\n", (unsigned long long)s->CycleCounter);
    fprintf(fp, "%o\n", s->ExtraCode);
    fprintf(fp, "%o\n", s->AllowInterrupt);
    fprintf(fp, "%o\n", s->PendFlag);
    fprintf(fp, "%o\n", s->PendDelay);
    fprintf(fp, "%o\n", s->ExtraDelay);
    fprintf(fp, "%o\n", s->OutputChannel7);
    for (int i = 0; i < 16; i++)
        fprintf(fp, "%o\n", s->OutputChannel10[i]);
    fprintf(fp, "%o\n", s->IndexValue);
    for (int i = 0; i < 1 + NUM_INTERRUPT_TYPES; i++)
        fprintf(fp, "%o\n", s->InterruptRequests[i]);
    fprintf(fp, "%o\n", s->InIsr);
    fprintf(fp, "%o\n", s->SubstituteInstruction);
    fprintf(fp, "%o\n", s->DownruptTimeValid);
    fprintf(fp, "%llo\n", (unsigned long long)s->DownruptTime);
    fprintf(fp, "%o\n", s->Downlink);

    fclose(fp);
    return 0;
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
