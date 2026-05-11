// test_find_keyrupt1 — locate KEYRUPT1's actual address by scanning
// fixed memory for its signature instruction sequence:
//   TS BANKRUPT   (= 0o42016)   — TS opcode (4) qtr (2) K=016
//   XCH Q         (= 0o46002)   — XCH opcode (4) qtr (3) K=02
//   TS QRUPT      (= 0o42012)   — TS opcode (4) qtr (2) K=012
//
// BANKRUPT = erasable 016, Q = 02, QRUPT = 012. Per
// ERASABLE_ASSIGNMENTS.agc lines 103, 112, 115.
//
// Search every fixed bank for the 3-instruction signature. Print the
// address(es) found.

#include "agc_harness.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"

#include <stdio.h>

extern agc_t *agc_core_state(void);

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    // Signature words (Block II encoding)
    // op-5 base = 050000, q=0 INDEX, q=1 DXCH, q=2 TS, q=3 XCH
    // TS K   = 054000 + K
    // XCH K  = 056000 + K
    int sig0 = 054016;  // TS BANKRUPT  (BANKRUPT = 016)
    int sig1 = 056002;  // XCH Q        (Q = 02)
    int sig2 = 054012;  // TS QRUPT     (QRUPT = 012)

    printf("Searching all 40 fixed banks for KEYRUPT1 signature:\n");
    printf("  expected: %05o %05o %05o (TS BANKRUPT; XCH Q; TS QRUPT)\n",
           sig0, sig1, sig2);
    int hits = 0;
    for (int bank = 0; bank < 40; bank++) {
        for (int off = 0; off < 02000 - 3; off++) {
            int w0 = st->Fixed[bank][off + 0] & 077777;
            int w1 = st->Fixed[bank][off + 1] & 077777;
            int w2 = st->Fixed[bank][off + 2] & 077777;
            if (w0 == sig0 && w1 == sig1 && w2 == sig2) {
                hits++;
                int z_addr = off + (bank < 4 ? (bank == 2 ? 04000 : 06000) : 02000);
                printf("  FOUND at Fixed[%d][%04o]  (banked Z=%05o, but real bank-relative Z=%05o)\n",
                       bank, off, z_addr, off + 02000);
                // Dump 20 instructions of context
                for (int j = off; j < off + 20 && j < 02000; j++) {
                    int w = st->Fixed[bank][j] & 077777;
                    int op = (w >> 12) & 07;
                    printf("    [%d][%04o] = %05o (op=%d)\n", bank, j, w, op);
                }
            }
        }
    }
    if (!hits) {
        printf("  *** NOT FOUND ***\n");
        printf("  Maybe TS encoding is different. Let me dump suspected\n");
        printf("  candidate words to debug:\n");
        // From INTERRUPT_LEAD_INS analysis, KEYRPTBB points to bank 14
        // (decimal). Sample first 5 words of every bank.
        for (int bank = 0; bank < 40; bank++) {
            int w0 = st->Fixed[bank][0] & 077777;
            int w1 = st->Fixed[bank][1] & 077777;
            int w2 = st->Fixed[bank][2] & 077777;
            printf("  bank %02d first 3 words: %05o %05o %05o\n",
                   bank, w0, w1, w2);
        }
    }

    PASS();
}
