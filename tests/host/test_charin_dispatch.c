// test_charin_dispatch — does CHARIN's first instruction actually run after
// a keypress arrives via the channel_router pump path?
//
// CHARIN starts at PINBALL_GAME__BUTTONS_AND_LIGHTS.agc:475 with:
//     CAF ONE
//     XCH DSPLOCK    ; DSPLOCK at erasable 01012 (bank 2 offset 012)
//     TS 21/22REG
//
// The XCH DSPLOCK should set DSPLOCK to +1 (from CAF ONE) the moment
// CHARIN starts. We use this as a yes/no signal for "did CHARIN dispatch?".
//
// Hardware observation (commits f14d954, c95a451): KEYRUPT1 dispatches
// and the full ISR runs through NOVAC + NOVAC2, but DSPLOCK never
// transitions to 1 in the trace window we captured. CHARIN evidently
// gets scheduled but never picked up by the executive.
//
// This host test is the deterministic reproduction. If it FAILS on
// host (DSPLOCK stays 0 after a posted keypress), we have a fast
// iteration loop that doesn't need a flash cycle. If it PASSES on
// host (DSPLOCK transitions to 1), the bug is hardware-specific
// (timing, dual-core preemption, FreeRTOS task switching) and host
// can't catch it.

#include "agc_harness.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"

#include <stdio.h>

extern agc_t *agc_core_state(void);

static int read_dsplock(agc_t *st) { return st->Erasable[2][012]; }

static void dump_slots(agc_t *st, const char *tag)
{
    printf("--- %s ---\n", tag);
    for (int slot = 0; slot < 8; slot++) {
        int base = 0154 + slot * 014;
        int mode    = st->Erasable[0][base + 7];   // MODE
        int loc     = st->Erasable[0][base + 8];   // LOC (the job's CADR)
        int bset    = st->Erasable[0][base + 9];   // BANKSET
        int pushloc = st->Erasable[0][base + 10];  // PUSHLOC
        int prio    = st->Erasable[0][base + 11];  // PRIORITY
        printf("  slot%d: MODE=%05o LOC=%05o BANKSET=%05o PUSHLOC=%05o PRIORITY=%05o\n",
               slot, mode & 077777, loc & 077777,
               bset & 077777, pushloc & 077777, prio & 077777);
    }
    int dsplock = read_dsplock(st);
    int newjob  = st->Erasable[0][0067];   // NEWJOB
    int loc_g   = st->Erasable[0][0164];   // LOC (the global one)
    int fixloc  = st->Erasable[0][0120];   // FIXLOC
    printf("  DSPLOCK=%05o NEWJOB=%05o GlobalLOC=%05o FIXLOC=%05o\n",
           dsplock & 077777, newjob & 077777, loc_g & 077777, fixloc & 077777);
}

// Hijack peripheral_stub_tick to test whether it's the cause.
// Defined as a weak override so the test can selectively neuter it
// without rebuilding the library.
#ifdef NEUTER_PSTUB
void peripheral_stub_tick(agc_t *state) { (void)state; }
void peripheral_stub_init(void) {}
#endif

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    // Step well past tick 16 (auto-RSET fire point) so the engine has
    // run KEYRUPT1, NOVAC, NOVAC2 in full. The hardware trace shows
    // this happens around cycle 131k.
    harness_step(200000);

    dump_slots(st, "after 200k cycles (post-auto-RSET)");
    int dsplock_a = read_dsplock(st);
    printf("DSPLOCK after auto-RSET: %05o\n", dsplock_a & 077777);

    // Post a manual keypress (V) and let the engine try to dispatch
    // CHARIN. If CHARIN runs, its first instruction sets DSPLOCK to +1.
    harness_post_key(/* DSKY_KEY_VERB */ 17);
    harness_step(50000);

    dump_slots(st, "after manual VERB key + 50k cycles");
    int dsplock_b = read_dsplock(st);
    printf("DSPLOCK after VERB key: %05o\n", dsplock_b & 077777);

    // Step a lot more in case CHARIN dispatch is delayed.
    harness_step(500000);

    dump_slots(st, "after VERB key + 550k cycles total");
    int dsplock_c = read_dsplock(st);
    printf("DSPLOCK after long step: %05o\n", dsplock_c & 077777);

    if (dsplock_c == 1) {
        printf("CHARIN ran (DSPLOCK transitioned to 1). Host harness "
               "does NOT reproduce the keypress-deafness bug; "
               "hardware-specific issue.\n");
    } else if (dsplock_c == 0) {
        printf("CHARIN did NOT run (DSPLOCK stayed 0). Host harness "
               "REPRODUCES the bug — fast iteration loop available.\n");
    } else {
        printf("DSPLOCK ended at %05o — neither 0 nor 1, "
               "something unusual happened.\n", dsplock_c & 077777);
    }

    // Engine alarm + execution state
    printf("\nEngine state at end:\n");
    printf("  RegZ=%05o  InIsr=%d  AllowInterrupt=%d  RestartLight=%d\n",
           st->Erasable[0][5], st->InIsr, st->AllowInterrupt, st->RestartLight);
    printf("  FAILREG=[%05o,%05o,%05o]  NightWatchman=%d  RuptLock=%d  TCTrap=%d\n",
           st->Erasable[0][0375] & 077777,
           st->Erasable[0][0376] & 077777,
           st->Erasable[0][0377] & 077777,
           st->NightWatchmanTripped, st->RuptLock, st->TCTrap);
    printf("  ch015=%05o\n", st->InputChannel[015] & 077777);

    // Standalone diagnostic - always PASS so the run is informative.
    PASS();
}
