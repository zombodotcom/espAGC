// test_hotloop_disasm — the Z-histogram showed engine pegged at Z=06070-06100
// in fixed-fixed bank 3. Dump those instruction words + trace one full pass
// through the loop so we can see what it's doing.

#include "agc_harness.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"

#include <stdio.h>
#include <stdlib.h>

extern agc_t *agc_core_state(void);
extern int    agc_engine(agc_t *State);

// Coarse Block-II opcode mnemonic decoder. Just enough to read a tight loop.
// Block II encoding: top 3 bits = opcode, remaining 12 bits = K.
//   000  TC
//   001  CCS / TCF (depends on K bit 12)
//   010  DAS / LXCH / INCR / ADS  (quartercodes)
//   011  CA / CS
//   100  INDEX / DXCH / TS / XCH (quartercodes)
//   101  AD / SU
//   110  MASK / TC / TCF (extracode-dependent)
//   111  MP / DV / BZF / MSU / QXCH / AUG / DIM / BZMF
// EXTEND prefix changes meaning. For sniff purposes the basic form is enough.
static const char *opname(int word)
{
    int op = (word >> 12) & 07;
    int q  = (word >> 10) & 03;   // quartercode
    int k  = word & 07777;
    static char buf[32];
    switch (op) {
        case 0: snprintf(buf, sizeof buf, "TC      %05o", k); break;
        case 1:
            if (q == 0)        snprintf(buf, sizeof buf, "CCS     %05o", k);
            else               snprintf(buf, sizeof buf, "TCF     %05o", k);
            break;
        case 2:
            switch (q) {
              case 0: snprintf(buf, sizeof buf, "DAS     %05o", k); break;
              case 1: snprintf(buf, sizeof buf, "LXCH    %05o", k); break;
              case 2: snprintf(buf, sizeof buf, "INCR    %05o", k); break;
              case 3: snprintf(buf, sizeof buf, "ADS     %05o", k); break;
            }
            break;
        case 3: snprintf(buf, sizeof buf, "CA/CS   %05o", k); break;
        case 4:
            switch (q) {
              case 0: snprintf(buf, sizeof buf, "INDEX   %05o", k); break;
              case 1: snprintf(buf, sizeof buf, "DXCH    %05o", k); break;
              case 2: snprintf(buf, sizeof buf, "TS      %05o", k); break;
              case 3: snprintf(buf, sizeof buf, "XCH     %05o", k); break;
            }
            break;
        case 5: snprintf(buf, sizeof buf, "AD/SU   %05o", k); break;
        case 6: snprintf(buf, sizeof buf, "MASK    %05o", k); break;
        case 7: snprintf(buf, sizeof buf, "MP/etc  %05o", k); break;
    }
    return buf;
}

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    // Run far enough for the engine to be settled into the hot loop.
    for (long c = 0; c < 500000; c++) agc_engine(st);

    printf("=== Snapshot at cycle 500000 ===\n");
    printf("Z=%05o A=%05o L=%05o Q=%05o FB=%05o EB=%05o BB=%05o\n",
           st->Erasable[0][5], st->Erasable[0][0], st->Erasable[0][1],
           st->Erasable[0][2], st->Erasable[0][4], st->Erasable[0][3],
           st->Erasable[0][6]);
    printf("RestartLight=%d InIsr=%d AllowInterrupt=%d ExtraDelay=%d\n",
           st->RestartLight, st->InIsr, st->AllowInterrupt, st->ExtraDelay);

    // Dump fixed-fixed words 06060..06120 (covers the hot range).
    printf("\n=== Fixed-fixed code at 06060..06120 ===\n");
    for (int a = 06060; a <= 06120; a++) {
        int bank = 3;
        int off  = a - 06000;
        int word = st->Fixed[bank][off] & 077777;
        // Mark current Z.
        char *marker = (a == (st->Erasable[0][5] & 07777)) ? " <-- Z" : "";
        printf("  %05o: %05o   %s%s\n", a, word, opname(word), marker);
    }

    // Now trace exactly 50 cycles starting from where we are.
    printf("\n=== 50-cycle trace ===\n");
    for (int t = 0; t < 50; t++) {
        int z   = st->Erasable[0][5] & 07777;
        int a   = st->Erasable[0][0] & 077777;
        int l   = st->Erasable[0][1] & 077777;
        int q   = st->Erasable[0][2] & 077777;
        int word = 0;
        if (z >= 04000 && z < 010000) {
            int bank = (z >= 06000) ? 3 : 2;
            int off  = z - (bank == 3 ? 06000 : 04000);
            word = st->Fixed[bank][off] & 077777;
        }
        int isr = st->InIsr;
        int xc  = st->ExtraCode;
        printf("  Z=%05o w=%05o %-14s A=%05o L=%05o Q=%05o isr=%d xc=%d\n",
               z, word, opname(word), a, l, q, isr, xc);
        agc_engine(st);
    }

    PASS();
}
