// test_restart_path — track key milestones during boot:
//   - every entry at Z=04000 (GOJAM)
//   - every change of NEWJOB (erasable 067)
//   - every change of MODREG  (erasable 0316 - current major mode)
//   - every change of FAILREG[0..2]
//   - every change of RestartLight
// Goal: see if the engine ever escapes restart, or if it's stuck in a
// PCLOOP→PTBAD→ALARM→DOFSTRT1→GOJAM loop.

#include "agc_harness.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"

#include <stdio.h>

extern agc_t *agc_core_state(void);
extern int    agc_engine(agc_t *State);

// Erasable addresses from Luminary099/ERASABLE_ASSIGNMENTS.agc.
#define NEWJOB_ADDR    0067   // bank 0
#define FAILREG0_ADDR  0375   // bank 0
#define FAILREG1_ADDR  0376
#define FAILREG2_ADDR  0377
#define MODREG_BANK    0      // MODREG is at offset 0316 bank 0... wait let me check
                              // Actually MODREG sits in unswitched erasable somewhere.
                              // We'll grep below.
// REDOCTR is the restart counter — incremented by GOPROG every time GOJAM fires.
// At offset... TBD. We'll trace it via Erasable[0][some_offset] if we find it.

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    int last_newjob   = st->Erasable[0][NEWJOB_ADDR];
    int last_fr0      = st->Erasable[0][FAILREG0_ADDR];
    int last_fr1      = st->Erasable[0][FAILREG1_ADDR];
    int last_fr2      = st->Erasable[0][FAILREG2_ADDR];
    int last_restart  = st->RestartLight;
    int last_z        = st->Erasable[0][5];
    int last_nw_trip  = st->NightWatchmanTripped;
    int last_rupt_lock= st->RuptLock;
    int last_no_tc    = st->NoTC;
    int last_tc_trap  = st->TCTrap;
    int last_no_rupt  = st->NoRupt;
    int last_parity   = st->ParityFail;
    // Track Z just BEFORE GOJAM to see what triggered it.
    int z_history[8] = {0};
    int z_history_idx = 0;

    long gojam_entries = 0;
    long gojam_first   = -1;
    long gojam_last    = -1;
    long pcloop_visits = 0;   // visits to Z=06070
    long dummyjob_visits = 0; // visits to Z near ADVAN (we'll guess once we know)

    long events_logged = 0;
    const long MAX_EVENTS = 500;
    // NoTC and TCTrap flap rapidly in yaAGC during normal operation —
    // they trip every ~850 cycles and clear within 2-3 cycles. Suppress
    // these unless they STAY set for >100 cycles (real failure).
    long no_tc_set_at = -1;
    long tc_trap_set_at = -1;

    const long CYCLES = 1500000;
    for (long c = 0; c < CYCLES; c++) {
        agc_engine(st);

        int z = st->Erasable[0][5];

        // GOJAM is at fixed-fixed 04000 (Luminary INTERRUPT_LEAD_INS.agc SETLOC 4000).
        if (z == 04000 && last_z != 04000) {
            gojam_entries++;
            if (gojam_first < 0) gojam_first = c;
            gojam_last = c;
            if (events_logged < MAX_EVENTS) {
                printf("[%8ld] GOJAM #%ld (from Z=%05o; last 8 Z: ",
                       c, gojam_entries, last_z & 07777);
                for (int i = 0; i < 8; i++) {
                    int idx = (z_history_idx + i) % 8;
                    printf("%05o ", z_history[idx] & 07777);
                }
                printf(") NW=%d RL=%d NoTC=%d TCT=%d NoR=%d PF=%d\n",
                       st->NightWatchmanTripped, st->RuptLock, st->NoTC,
                       st->TCTrap, st->NoRupt, st->ParityFail);
                events_logged++;
            }
        }
        // Maintain Z history.
        z_history[z_history_idx] = last_z;
        z_history_idx = (z_history_idx + 1) % 8;

        // Log watchdog edges.
        if (st->NightWatchmanTripped != last_nw_trip && events_logged < MAX_EVENTS) {
            printf("[%8ld] NW_TRIPPED: %d -> %d  (z=%05o)\n",
                   c, last_nw_trip, st->NightWatchmanTripped, z & 07777);
            last_nw_trip = st->NightWatchmanTripped; events_logged++;
        }
        if (st->RuptLock != last_rupt_lock && events_logged < MAX_EVENTS) {
            printf("[%8ld] RuptLock: %d -> %d  (z=%05o)\n",
                   c, last_rupt_lock, st->RuptLock, z & 07777);
            last_rupt_lock = st->RuptLock; events_logged++;
        }
        if (st->NoTC != last_no_tc) {
            if (st->NoTC) no_tc_set_at = c;
            else if (no_tc_set_at >= 0 && (c - no_tc_set_at) > 50 &&
                     events_logged < MAX_EVENTS) {
                printf("[%8ld] NoTC stayed set %ld cyc  (z=%05o)\n",
                       c, c - no_tc_set_at, z & 07777);
                events_logged++;
            }
            last_no_tc = st->NoTC;
        }
        if (st->TCTrap != last_tc_trap) {
            if (st->TCTrap) tc_trap_set_at = c;
            else if (tc_trap_set_at >= 0 && (c - tc_trap_set_at) > 50 &&
                     events_logged < MAX_EVENTS) {
                printf("[%8ld] TCTrap stayed set %ld cyc  (z=%05o)\n",
                       c, c - tc_trap_set_at, z & 07777);
                events_logged++;
            }
            last_tc_trap = st->TCTrap;
        }
        if (st->NoRupt != last_no_rupt && events_logged < MAX_EVENTS) {
            printf("[%8ld] NoRupt: %d -> %d  (z=%05o)\n",
                   c, last_no_rupt, st->NoRupt, z & 07777);
            last_no_rupt = st->NoRupt; events_logged++;
        }
        if (st->ParityFail != last_parity && events_logged < MAX_EVENTS) {
            printf("[%8ld] ParityFail: %d -> %d  (z=%05o)\n",
                   c, last_parity, st->ParityFail, z & 07777);
            last_parity = st->ParityFail; events_logged++;
        }

        if (z == 06070) pcloop_visits++;

        int newjob = st->Erasable[0][NEWJOB_ADDR];
        int fr0    = st->Erasable[0][FAILREG0_ADDR];
        int fr1    = st->Erasable[0][FAILREG1_ADDR];
        int fr2    = st->Erasable[0][FAILREG2_ADDR];
        int restart = st->RestartLight;

        if (newjob != last_newjob && events_logged < MAX_EVENTS) {
            printf("[%8ld] NEWJOB: %05o -> %05o  (z=%05o)\n",
                   c, last_newjob & 077777, newjob & 077777, z & 07777);
            last_newjob = newjob;
            events_logged++;
        }
        if (fr0 != last_fr0 && events_logged < MAX_EVENTS) {
            printf("[%8ld] FAILREG[0]: %05o -> %05o  (z=%05o)\n",
                   c, last_fr0 & 077777, fr0 & 077777, z & 07777);
            last_fr0 = fr0;
            events_logged++;
        }
        if (fr1 != last_fr1 && events_logged < MAX_EVENTS) {
            printf("[%8ld] FAILREG[1]: %05o -> %05o  (z=%05o)\n",
                   c, last_fr1 & 077777, fr1 & 077777, z & 07777);
            last_fr1 = fr1;
            events_logged++;
        }
        if (fr2 != last_fr2 && events_logged < MAX_EVENTS) {
            printf("[%8ld] FAILREG[2]: %05o -> %05o  (z=%05o)\n",
                   c, last_fr2 & 077777, fr2 & 077777, z & 07777);
            last_fr2 = fr2;
            events_logged++;
        }
        if (restart != last_restart && events_logged < MAX_EVENTS) {
            printf("[%8ld] RestartLight: %d -> %d  (z=%05o)\n",
                   c, last_restart, restart, z & 07777);
            last_restart = restart;
            events_logged++;
        }

        last_z = z;
    }

    printf("\n=== Summary over %ld cycles ===\n", CYCLES);
    printf("GOJAM entries (Z=04000): %ld   first=%ld last=%ld\n",
           gojam_entries, gojam_first, gojam_last);
    printf("Z=06070 visits:          %ld\n", pcloop_visits);
    printf("Final RestartLight=%d, NEWJOB=%05o, FAILREG=[%05o,%05o,%05o]\n",
           st->RestartLight, st->Erasable[0][NEWJOB_ADDR] & 077777,
           st->Erasable[0][FAILREG0_ADDR] & 077777,
           st->Erasable[0][FAILREG1_ADDR] & 077777,
           st->Erasable[0][FAILREG2_ADDR] & 077777);

    PASS();
}
