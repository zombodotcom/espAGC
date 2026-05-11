// test_z_histogram — where does the engine actually live?
//
// We know after 5M cycles RestartLight=1, NEWJOB=0, all DSKY digits blank.
// That means the engine is stuck somewhere — but where? Build a histogram
// of Z (program counter) values over a long run, print the top hot spots.
// Cross-reference with Luminary099 listings to find what code keeps running.
//
// Z is a 12-bit erasable register (offset 5 in bank 0), but the *effective*
// address can be in fixed-fixed (04000-07777) or banked (10000-17777 with
// FBANK selecting bank 0-31). We log raw Z plus FBANK so banked addresses
// can be resolved.
//
// This is investigation, no assertions.

#include "agc_harness.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern agc_t *agc_core_state(void);
extern int    agc_engine(agc_t *State);

// Z is 12 bits, FBANK in RegFB occupies bits 11-15 (bank number << 10).
// 4096 raw Z values * 32 banks = 131072 slots — but we only really care
// when Z >= 02000 (fixed memory). Use a flat 4096-entry array on raw Z
// for simplicity; print FBANK alongside.

#define ZMAX 010000  /* 4096 */

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    static unsigned long counts[ZMAX];
    static unsigned long fbank_counts[ZMAX][8];  /* top 8 banks at each Z */
    static int fbank_seen[ZMAX][8];

    memset(counts, 0, sizeof(counts));
    memset(fbank_counts, 0, sizeof(fbank_counts));
    memset(fbank_seen, 0, sizeof(fbank_seen));

    const long CYCLES = 2000000;
    long isr_cycles = 0;
    long allow_interrupt_cycles = 0;

    for (long c = 0; c < CYCLES; c++) {
        agc_engine(st);
        int z   = st->Erasable[0][5] & 07777;
        int fb  = (st->Erasable[0][4] >> 10) & 037;
        counts[z]++;
        if (st->InIsr) isr_cycles++;
        if (st->AllowInterrupt) allow_interrupt_cycles++;

        // Track which FBANK was active for this Z. Use a tiny LRU.
        int slot = -1;
        for (int i = 0; i < 8; i++) {
            if (fbank_seen[z][i] && fbank_counts[z][i] != 0 &&
                (int)((fbank_counts[z][i] >> 24) & 037) == fb) {
                slot = i; break;
            }
        }
        if (slot < 0) {
            // Find an unused slot.
            for (int i = 0; i < 8; i++) {
                if (!fbank_seen[z][i]) { slot = i; fbank_seen[z][i] = 1; break; }
            }
        }
        if (slot < 0) slot = 7;  // overwrite last
        // Encode FBANK in top byte, count in lower bytes.
        unsigned long prev = fbank_counts[z][slot] & 0x00FFFFFF;
        fbank_counts[z][slot] = ((unsigned long)fb << 24) | ((prev + 1) & 0x00FFFFFF);
    }

    // Final state.
    printf("After %ld cycles:\n", CYCLES);
    printf("  RestartLight=%d Standby=%d InIsr=%d AllowInterrupt=%d\n",
           st->RestartLight, st->Standby, st->InIsr, st->AllowInterrupt);
    printf("  NightWatchman=%d NightWatchmanTripped=%d\n",
           st->NightWatchman, st->NightWatchmanTripped);
    printf("  Z=%05o (effective addr in fixed/banked memory)\n",
           st->Erasable[0][5]);
    printf("  RegA=%05o RegL=%05o RegQ=%05o\n",
           st->Erasable[0][0], st->Erasable[0][1], st->Erasable[0][2]);
    printf("  RegFB=%05o RegEB=%05o RegBB=%05o\n",
           st->Erasable[0][4], st->Erasable[0][3], st->Erasable[0][6]);
    printf("  ISR cycles: %ld (%.2f%%)\n", isr_cycles, 100.0 * isr_cycles / CYCLES);
    printf("  AllowInterrupt cycles: %ld (%.2f%%)\n",
           allow_interrupt_cycles, 100.0 * allow_interrupt_cycles / CYCLES);
    printf("  ExtraDelay=%d PendDelay=%d PendFlag=%d\n",
           st->ExtraDelay, st->PendDelay, st->PendFlag);

    // Sort top 40 hottest Z values.
    typedef struct { int z; unsigned long n; } pair_t;
    pair_t top[40];
    int ntop = 0;
    for (int z = 0; z < ZMAX; z++) {
        if (counts[z] == 0) continue;
        if (ntop < 40) {
            top[ntop].z = z; top[ntop].n = counts[z]; ntop++;
        } else {
            // Find min in top, replace if larger.
            int imin = 0;
            for (int i = 1; i < ntop; i++) if (top[i].n < top[imin].n) imin = i;
            if (counts[z] > top[imin].n) {
                top[imin].z = z; top[imin].n = counts[z];
            }
        }
    }
    // Bubble sort descending (small N).
    for (int i = 0; i < ntop; i++) {
        for (int j = i + 1; j < ntop; j++) {
            if (top[j].n > top[i].n) {
                pair_t t = top[i]; top[i] = top[j]; top[j] = t;
            }
        }
    }

    printf("\nTop 40 hottest Z addresses (raw Z, %% of cycles, FBANK breakdown):\n");
    for (int i = 0; i < ntop; i++) {
        int z = top[i].z;
        printf("  Z=%05o  %8lu cyc (%5.2f%%)  banks:",
               z, top[i].n, 100.0 * top[i].n / CYCLES);
        for (int j = 0; j < 8; j++) {
            if (!fbank_seen[z][j]) continue;
            int fb = (fbank_counts[z][j] >> 24) & 037;
            unsigned long n = fbank_counts[z][j] & 0x00FFFFFF;
            if (n == 0) continue;
            printf(" fb=%02o:%lu", fb, n);
        }
        printf("\n");
    }

    // Count distinct Z values executed (working-set size).
    int distinct = 0;
    for (int z = 0; z < ZMAX; z++) if (counts[z]) distinct++;
    printf("\nDistinct Z addresses visited: %d / %d\n", distinct, ZMAX);

    PASS();
}
