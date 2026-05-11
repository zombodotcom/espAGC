// test_cadr_resolution — pin hypothesis 1 from NEXT_SESSION.md.
//
// The previous diagnostic (test_executive_state) showed slot 0 of the
// executive PRIORITY array gets CADR=077615 after KEYRUPT1 fires —
// which decodes to bank 037 offset 01615, an invalid code address.
// That value comes from NOVAC's:
//
//     INDEX Q       ; Q = address of first word after `TC NOVAC`
//     DCA 0         ; A,L = the two words at Q (the 2CADR literal)
//     DXCH NEWLOC   ; -> NEWLOC, NEWLOC+1, then eventually PRIORITY+1
//
// In KEYRUPT1 (KEYRUPT,_UPRUPT.agc:51-54), the relevant fragment is:
//
//     ACCEPTUP   CAF   CHRPRIO     ; 04037
//                TC    NOVAC       ; 04040
//                EBANK= DSPCOUNT   ; (directive, no word)
//                2CADR CHARIN      ; 04041, 04042
//                CA    RUPTREG4    ; 04043
//
// So the 2CADR words live at fixed addresses 04041, 04042 — which in
// yaAGC's Fixed[] array map to Fixed[2][041], Fixed[2][042] (bank-2
// fixed-fixed, addresses 04000-05777).
//
// This test compares what's in fixed memory against what ends up in
// slot-0 CADR after KEYRUPT1. Three possible outcomes:
//
//   (A) Fixed[2][041..042] == slot 0 CADR words
//       -> 2CADR words ARE being read correctly; the bug is downstream
//       (slot-allocation, FBANK setup, or yaYUL produced a bad CADR
//       to begin with — i.e., the ROM has the wrong words).
//
//   (B) Fixed[2][041..042] != slot 0 CADR words AND Fixed words look
//       plausible (high bits set, nonzero)
//       -> Our engine's DCA / interrupt-time PC is wrong.
//
//   (C) Fixed[2][041..042] look like 077615 themselves
//       -> Our ROM has bad bytes at this offset (parity/parsing error
//       during ROM load).
//
// The test PASSes unconditionally and just prints the diagnostic data.
// Read the output to decide which hypothesis is alive.
//
// Result (2026-05-11 run, see NEXT_SESSION.md notes for that date):
//   - Fixed[2][041..042] = 034062, 056006 (correct, ROM-loaded).
//   - Across the 120k-250k cycle window slot 0 never hits 077615.
//   - The 077615 pathology that test_executive_state saw on/near hardware
//     does NOT reproduce in the host harness. The bug is hardware-specific
//     (FreeRTOS task timing, interrupt-delivery race, or our integration's
//     init differs from upstream agc_engine_init.c). Standalone diagnostic;
//     not added to the regular Makefile test list — it would over-pass.

#include "agc_harness.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"

#include <stdio.h>

extern agc_t *agc_core_state(void);

static void dump_priority(agc_t *st, const char *tag)
{
    printf("--- PRIORITY array @ %s ---\n", tag);
    for (int slot = 0; slot < 7; slot++) {
        int p    = st->Erasable[0][0167 + slot*014];
        int cadr = st->Erasable[0][0170 + slot*014];
        printf("  slot%d: PRIO=%06o CADR=%06o\n", slot, p, cadr);
    }
    printf("  NEWLOC=%06o NEWLOC+1=%06o  NEWPRIO=%06o\n",
           st->Erasable[0][0163], st->Erasable[0][0164],
           st->Erasable[0][0162]);
    printf("  FBANK=%06o BBANK=%06o\n",
           st->InputChannel[004], st->InputChannel[006]);
}

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    // --- BASELINE: dump KEYRUPT1's machine code BEFORE running anything ---
    // KEYRUPT1 starts at fixed 04024. Walk the first ~20 words.
    // Fixed bank 2 covers 04000-05777, so address 04024 = Fixed[2][024].
    printf("=== KEYRUPT1 fixed memory @ Fixed[2][024..050] (pre-boot) ===\n");
    for (int off = 024; off <= 050; off++) {
        int w = st->Fixed[2][off] & 077777;
        printf("  %05o : %06o\n", 04000 + off, w);
    }
    int cadr_lo = st->Fixed[2][041] & 077777;
    int cadr_hi = st->Fixed[2][042] & 077777;
    printf("\n*** 2CADR CHARIN words: Fixed[2][041]=%06o  Fixed[2][042]=%06o ***\n",
           cadr_lo, cadr_hi);

    // --- Walk past auto-RSET at tick 16 (~131k cycles) sampling slot 0 ---
    // The previous diagnostic (test_executive_state) caught slot 0 with
    // CADR=077615 just after auto-RSET at ~135k cycles. We want to land
    // ON that moment, so sample at fine granularity from 120k to 250k.
    int sample_at[] = {120000, 130000, 132000, 134000, 135000, 137000,
                       140000, 145000, 150000, 160000, 180000, 200000, 250000};
    int n_samples = sizeof(sample_at) / sizeof(sample_at[0]);
    int prev = 0;
    int saw_bad_cadr = 0;
    int bad_cadr_val = 0;
    int bad_cadr_cycle = 0;
    for (int i = 0; i < n_samples; i++) {
        int delta = sample_at[i] - prev;
        if (delta > 0) harness_step(delta);
        prev = sample_at[i];
        int p0 = st->Erasable[0][0167];
        int c0 = st->Erasable[0][0170];
        int p1 = st->Erasable[0][0167 + 014];
        int c1 = st->Erasable[0][0170 + 014];
        int z  = st->Erasable[0][RegZ];
        printf("  cyc=%6d  slot0 PRIO=%06o CADR=%06o   slot1 PRIO=%06o CADR=%06o  RegZ=%05o\n",
               sample_at[i], p0, c0, p1, c1, z);
        // Bad CADR signatures: offset < 02000 in any non-zero bank field,
        // OR matches the previously-observed 077615 value, OR FBANK>=040.
        int off = c0 & 01777;
        int fbank = (c0 >> 10) & 037;
        if (c0 != 0 && (off < 02000 || fbank >= 040 || c0 == 077615)) {
            saw_bad_cadr = 1;
            bad_cadr_val = c0;
            bad_cadr_cycle = sample_at[i];
        }
    }

    dump_priority(st, "final (250k cycles)");

    // The previous test saw slot 0 with PRIO=030110 CADR=077615 in the
    // 130k-140k window. If we caught it in any sample, report.
    int slot0_prio = st->Erasable[0][0167];
    int slot0_cadr = st->Erasable[0][0170];
    printf("\n=== ANALYSIS ===\n");
    if (saw_bad_cadr) {
        printf("  BAD CADR seen: %06o at cycle %d\n", bad_cadr_val, bad_cadr_cycle);
    } else {
        printf("  No structurally-invalid CADR caught in samples 120k-250k.\n"
               "  The 077615 from the previous diagnostic may have been a\n"
               "  transient, OR may only reproduce on hardware. Re-check\n"
               "  with finer sampling around 130k.\n");
    }
    printf("  slot0 priority = %06o  (expect %06o = CHRPRIO+FAKEPRET)\n",
           slot0_prio, 030110);
    printf("  slot0 CADR     = %06o\n", slot0_cadr);
    printf("  2CADR word A   = %06o  (Fixed[2][041], read by DCA)\n", cadr_lo);
    printf("  2CADR word B   = %06o  (Fixed[2][042], read by DCA)\n", cadr_hi);

    // The DXCH after DCA swaps A and L, so word B (from Fixed[2][042])
    // lands in NEWLOC and word A (from Fixed[2][041]) in NEWLOC+1.
    // Then NOVAC2 copies NEWLOC to PRIORITY+1 (CADR). So slot0 CADR
    // should equal word B (Fixed[2][042]).
    if (slot0_cadr == 0) {
        printf("  -> slot 0 NEVER POPULATED — KEYRUPT1's NOVAC call never ran,\n"
               "     OR our channel_router_pump_input didn't deliver the key.\n");
    } else if (slot0_cadr == cadr_hi) {
        printf("  -> CADR matches Fixed[2][042]. 2CADR is read correctly.\n"
               "     Bug is NOT in 2CADR resolution. Look at slot dispatch or\n"
               "     at the CADR's actual decode (bank + offset).\n");
    } else if (slot0_cadr == cadr_lo) {
        printf("  -> CADR matches Fixed[2][041]. DXCH order may be inverted —\n"
               "     check our engine's DCA/DXCH implementation.\n");
    } else {
        printf("  -> CADR matches NEITHER fixed word. Engine bug between\n"
               "     DCA and PRIORITY array store (NOVAC2, FBANK setup,\n"
               "     or DXCH). Single-step needed.\n");
    }

    // Decode slot0 CADR as if it were a BBCON-style word:
    //   bits 14:10 = FBANK (5 bits)   bits 9:0 = offset within bank
    // If FBANK >= 040, the CADR is bogus (we only have banks 0-043).
    int fbank = (slot0_cadr >> 10) & 037;
    int eflag = (slot0_cadr >> 15) & 01;
    int off   = slot0_cadr & 01777;
    printf("\n  CADR decoded as: sign/E=%d  FBANK=%02o(%d)  offset=%04o(%d)\n",
           eflag, fbank, fbank, off, off);
    if (off < 02000) {
        printf("  -> offset %04o is BELOW fixed-fixed start (02000), invalid.\n",
               off);
    }

    PASS();
}
