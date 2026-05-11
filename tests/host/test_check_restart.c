// test_check_restart — count GOJAM transitions and NightWatchman trips
// across 200k cycles after VERB key. If the engine is restarting,
// slots get cleared without the jobs actually running.

#include "agc_harness.h"
#include "test_helpers.h"
#include "yaAGC.h"
#include "agc_engine.h"
#include "dsky_keys.h"
#include <stdio.h>

extern agc_t *agc_core_state(void);
extern int agc_engine(agc_t *st);
extern int ShowAlarms;

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();
    ShowAlarms = 1;
    for (long c = 0; c < 1000000; c++) agc_engine(st);
    ShowAlarms = 0;

    printf("Before VERB: NW=%d TC=%d NoTC=%d RuptLock=%d Restart=%d\n",
           st->NightWatchmanTripped, st->TCTrap, st->NoTC,
           st->RuptLock, st->RestartLight);

    harness_post_key(DSKY_KEY_VERB);

    int prev_nw = st->NightWatchmanTripped;
    int prev_restart = st->RestartLight;
    int prev_isr = st->InIsr;
    int gojam_via_nw = 0, restart_changes = 0, isr_entries = 0;
    int z02000_hits = 0;  // Z=02000 is GOJAM entry
    int prev_z = -1;

    for (long c = 0; c < 200000; c++) {
        agc_engine(st);
        int z = st->Erasable[0][5] & 07777;
        if (z == 02000 && prev_z != 02000) z02000_hits++;
        prev_z = z;
        if (st->NightWatchmanTripped && !prev_nw) gojam_via_nw++;
        prev_nw = st->NightWatchmanTripped;
        if (st->RestartLight != prev_restart) restart_changes++;
        prev_restart = st->RestartLight;
        if (st->InIsr && !prev_isr) isr_entries++;
        prev_isr = st->InIsr;
    }

    printf("In 200k cycles after VERB:\n");
    printf("  Z=02000 hits (GOJAM): %d\n", z02000_hits);
    printf("  NightWatchman trips: %d\n", gojam_via_nw);
    printf("  RestartLight transitions: %d\n", restart_changes);
    printf("  ISR entries: %d\n", isr_entries);
    printf("ch77 final: %05o (NW=%d TC=%d RL=%d PF=%d)\n",
           st->InputChannel[077] & 077777,
           (st->InputChannel[077] >> 4) & 1,
           (st->InputChannel[077] >> 2) & 1,
           (st->InputChannel[077] >> 3) & 1,
           st->InputChannel[077] & 1);
    printf("Final state: NW=%d TC=%d NoTC=%d RuptLock=%d Restart=%d cyc=%u\n",
           st->NightWatchmanTripped, st->TCTrap, st->NoTC,
           st->RuptLock, st->RestartLight,
           (unsigned)(st->CycleCounter & 0xFFFFFFFF));
    PASS();
}
