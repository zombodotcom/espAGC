// test_canonical_match — strip our integration down to what canonical
// Pi/Linux yaAGC+LM_Simulator+yaDSKY does, then run V36E V37E 00E V37E 00E
// and check whether PRG=00 (OC10[11]=55265) emits.
//
// What's stripped:
//   - No peripheral_stub at all (no init, no tick, no rescues, no step,
//     no force_dispatch).
//   - No channel_router (no key ring, no pump throttle, no auto-RSET).
//   - No agc_init.c slot pre-zero. Linked against UPSTREAM
//     third_party/virtualagc/yaAGC/agc_engine_init.c which initialises
//     identically to the Pi/Linux canonical setup.
//
// What's matched to canonical:
//   - Cold-boot defaults: ch030=037777, ch031..033=077777.
//   - Real-time pacing at AGC_PER_SECOND = 85333 cyc/sec (matches yaAGC
//     SimExecute on Linux).
//   - LM_INI socket writes sent ~0.5 s after boot to ch030..033 (matches
//     wsl_reliability_test.sh / capture_with_dumps.sh).
//   - Key sequence "R V36E V37E 00E V37E 00E" at 100 ms between keys,
//     3 s between tokens (identical to the WSL reference).
//   - ch015 + InterruptRequests[5]=1 for keypresses (canonical KEYRUPT1
//     path via SocketAPI.c lines 240-241).
//
// Expected outcome on canonical WSL: 5/5 succeed (verified by
//   tests/host/wsl_reliability_test.sh — see commit log).
// Expected outcome here: tells us whether our integration features are
// the bug, or whether agc_engine.c itself behaves differently than the
// Linux build for some reason.
//
// Build: mingw32-make test_canonical_match.exe
// Run:   ROM=../../build/roms/Luminary099.bin ./test_canonical_match.exe

#include "yaAGC.h"
#include "agc_engine.h"

#include <pthread.h>
#include <stdint.h>
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

// Upstream agc_engine_init from third_party/virtualagc/yaAGC. Same one
// the canonical Pi/Linux yaAGC uses.
extern int agc_engine_init(agc_t *State, const char *RomImage,
                            const char *CoreDump, int AllOrErasable);

static agc_t state;
agc_t *agc_core_state(void) { return &state; }

// ---- key driver --------------------------------------------------------
static volatile int g_pending_key = -1;
static volatile int g_pending_lm_ini = 0;

#define MS 1000

// Maps the same chars wsl_reliability_test.sh maps. ENTR is 'E'.
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

// Driver thread: LM_INI delay configurable via LM_INI_MS env var (default 500ms
// matching wsl_reliability_test.sh). Lower values test whether sending LM_INI
// before 1/ACCSET enters the interpretive deadlock helps us escape.
static int g_lm_ini_delay_ms = 500;
static void *driver(void *arg) {
    const char *seq = (const char *)arg;
    sleep_us((long)g_lm_ini_delay_ms * MS);
    g_pending_lm_ini = 1;
    while (g_pending_lm_ini) sleep_us(1000);
    sleep_us(2000 * MS);
    const char *p = seq;
    while (*p) {
        if (*p == ' ') { sleep_us(3000 * MS); p++; continue; }
        int code = ascii_to_key(*p);
        if (code >= 0) {
            while (g_pending_key >= 0) sleep_us(1000);   // wait for last to be consumed
            g_pending_key = code;
            sleep_us(100 * MS);
        }
        p++;
    }
    sleep_us(2000 * MS);
    return NULL;
}

int main(int argc, char **argv) {
    const char *rom = getenv("ROM");
    if (!rom) rom = "../../build/roms/Luminary099.bin";
    const char *seq = getenv("SEQ");
    if (!seq) seq = "R V36E V37E 00E V37E 00E";
    const char *lm_env = getenv("LM_INI_MS");
    if (lm_env && *lm_env) g_lm_ini_delay_ms = atoi(lm_env);

    int rc = agc_engine_init(&state, rom, NULL, 0);
    if (rc != 0) {
        fprintf(stderr, "agc_engine_init failed: %d (rom=%s)\n", rc, rom);
        return 1;
    }
    printf("agc_engine_init OK   seq='%s'\n", seq);
    printf("boot ch030=%05o ch031=%05o ch032=%05o ch033=%05o (upstream defaults)\n",
           state.InputChannel[030], state.InputChannel[031],
           state.InputChannel[032], state.InputChannel[033]);

    // LM_INI from wsl_reliability_test.sh:
    //   (0o30, 0o36331), (0o31, 0o77777), (0o32, 0o22777), (0o33, 0o57776)
    // We skip the channel-mask packets — those just authorise the write;
    // WriteIO on host has no mask gating.

    // FAST mode: run engine as fast as possible, inject at fixed cycle
    // counts. Removes timing variability completely. Set FAST=1 to enable.
    int fast_mode = getenv("FAST") && atoi(getenv("FAST"));
    pthread_t tid;
    if (!fast_mode) pthread_create(&tid, NULL, driver, (void *)seq);

    // 30 s of simulated time at AGC_PER_SECOND = 85333 cyc/sec.
    const long total_cycles = 85333L * 30;
    const uint64_t us_num = 1000000ULL;
    const uint64_t us_den = 85333ULL;
    const int batch = 256;
    uint64_t t0 = now_us();
    long elapsed = 0;
    int hit_55265 = 0;
    long hit_at_cycle = 0;
    long last_oc11 = -1;

    // FAST mode schedule: inject LM_INI + keys at fixed cycles matching
    // WSL canonical's wall-clock-to-cycle conversion (AGC_PER_SECOND).
    // Sequence "R V36E V37E 00E V37E 00E" at 100ms/key + 3s/token gaps,
    // starting after 2.5s settle, LM_INI at 0.5s.
    int fast_step = 0;
    long fast_schedule[64];
    int  fast_codes[64];
    int  fast_n = 0;
    if (fast_mode) {
        long cyc_per_ms = 85;   // AGC_PER_SECOND / 1000
        long lm_at = (long)g_lm_ini_delay_ms * cyc_per_ms;
        fast_schedule[fast_n] = lm_at; fast_codes[fast_n++] = -1;  // -1 = LM_INI marker
        long t_cyc = lm_at + 2000 * cyc_per_ms;     // 2 s after LM_INI
        const char *p = seq;
        while (*p) {
            if (*p == ' ') { t_cyc += 3000 * cyc_per_ms; p++; continue; }
            int code = ascii_to_key(*p);
            if (code >= 0) {
                fast_schedule[fast_n] = t_cyc;
                fast_codes[fast_n++] = code;
                t_cyc += 100 * cyc_per_ms;
            }
            p++;
        }
    }

    while (elapsed < total_cycles) {
        // Async-thread-driven injection (default).
        // CRITICAL: in canonical SocketAPI, packets are read one-at-a-time
        // with SocketInterlaceReload=50 cycles between reads. So the 4
        // LM_INI channel writes happen at 50-cycle intervals, NOT all on
        // one cycle. Replicate that here.
        if (!fast_mode && g_pending_lm_ini) {
            static int lm_phase = 0;
            static long last_lm_cycle = 0;
            if (lm_phase == 0 || elapsed - last_lm_cycle >= 50) {
                // Match canonical SocketAPI mask+value flow exactly. The
                // Python driver (wsl_reliability_test.sh / windows_yaagc_test.py)
                // sends ONE mask packet then ONE value packet per channel.
                // SocketAPI applies: Value = (incoming_value & mask)
                //                          | (ReadIO(ch) & ~mask)
                // BEFORE calling WriteIO. For ch033 the mask is 077776 (bit 0
                // preserved), so the value WriteIO actually sees is 057777,
                // not 057776 — which after the engine's ch033 special-mask
                // ((current & 076000) | (Value & 001777)) gives ch033=077777,
                // NOT 077776. Bit 0 of ch033 stays set.
                //
                // Likewise ch016 receives mask=000174 + value=0, which is a
                // bits-2..6-clear no-op against ch016's cold-boot 0.
                int chs[]  = { 016, 030, 031, 032, 033 };
                int vals[] = {     0, 036331, 077777, 022777, 057776 };
                int masks[]= { 000174, 077777, 077777, 077777, 077776 };
                if (lm_phase < 5) {
                    int ch   = chs[lm_phase];
                    int val  = vals[lm_phase];
                    int mask = masks[lm_phase];
                    int cur  = state.InputChannel[ch] & 077777;
                    int merged = (val & mask) | (cur & ~mask);
                    WriteIO(&state, ch, merged);
                    printf("[%.3f s, cyc=%ld] LM_INI[%d]: ch%03o = %05o (raw=%05o mask=%05o cur=%05o)\n",
                           (now_us() - t0) / 1e6, elapsed,
                           lm_phase, ch, merged, val, mask, cur);
                    last_lm_cycle = elapsed;
                    lm_phase++;
                    if (lm_phase >= 5) { g_pending_lm_ini = 0; lm_phase = 0; }
                }
            }
        }
        if (!fast_mode) {
            int k = g_pending_key;
            if (k >= 0) {
                WriteIO(&state, 015, k & 037);
                state.InterruptRequests[5] = 1;
                printf("[%.3f s, cyc=%ld] key=%02o (KEYRUPT1 armed)\n",
                       (now_us() - t0) / 1e6, elapsed, k & 037);
                g_pending_key = -1;
            }
        } else {
            // Fire any scheduled events whose cycle threshold passed.
            while (fast_step < fast_n && elapsed >= fast_schedule[fast_step]) {
                int code = fast_codes[fast_step++];
                if (code < 0) {
                    WriteIO(&state, 030, 036331);
                    WriteIO(&state, 031, 077777);
                    WriteIO(&state, 032, 022777);
                    WriteIO(&state, 033, 057776);
                    printf("[cyc=%ld] LM_INI\n", elapsed);
                } else {
                    WriteIO(&state, 015, code & 037);
                    state.InterruptRequests[5] = 1;
                    printf("[cyc=%ld] key=%02o\n", elapsed, code & 037);
                }
            }
        }
        int this_batch = batch;
        if (elapsed + this_batch > total_cycles) this_batch = total_cycles - elapsed;
        for (int i = 0; i < this_batch; i++) {
            agc_engine(&state);
            elapsed++;
            int oc11 = state.OutputChannel10[11] & 077777;
            if (oc11 != last_oc11) {
                last_oc11 = oc11;
                if (oc11 == 055265 && !hit_55265) {
                    hit_55265 = 1;
                    hit_at_cycle = elapsed;
                    printf("[%.3f s, cyc=%ld] *** PRG=00 EMITTED (OC10[11]=55265) ***\n",
                           (now_us() - t0) / 1e6, elapsed);
                }
            }
        }
        // Pace, unless FAST mode is on.
        if (!fast_mode) {
            uint64_t target = t0 + (uint64_t)elapsed * us_num / us_den;
            uint64_t now = now_us();
            while (now < target) {
                int64_t gap = (int64_t)(target - now);
                if (gap > 1500) sleep_us((long)gap);
                else break;
                now = now_us();
            }
        }
    }
    if (!fast_mode) pthread_join(tid, NULL);

    printf("\nFINAL: cyc=%ld OC10[11]=%05o (last=%05lo) Z=%05o "
           "active_prio=%06o LOC=%06o\n",
           elapsed, state.OutputChannel10[11] & 077777, last_oc11,
           state.Erasable[0][5] & 077777,
           state.Erasable[0][0167] & 077777,
           state.Erasable[0][0164] & 077777);
    if (hit_55265) {
        printf("RESULT: PRG=00 SUCCESS  (emitted at cycle %ld, %.3f s)\n",
               hit_at_cycle, hit_at_cycle * 1.0 / 85333);
        return 0;
    } else {
        printf("RESULT: PRG=00 NOT REACHED\n");
        return 1;
    }
}
