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
    peripheral_stub_init();   // seed MASS / DAPBOOLS so 1/ACCS converges
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
