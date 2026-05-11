// test_stuck_loop_state — full state snapshot at multiple visits into
// the 06647..06674 stuck interpreter loop. We need to see what value
// of RegA gets the CCS at 06664 to fall through to 06667 (and thus
// loop back to 06647 via 06674 TCF 06647), vs branch out to 06247 or
// 06743.
//
// CCS K branches as:
//   A>+0 -> PC+1 (06665 -> TCF 06247) EXIT
//   A=+0 -> PC+2 (06666 -> TCF 06743) EXIT
//   A<-0 -> PC+3 (06667 -> CA 00120 NNTYPTEM) LOOP
//   A=-0 -> PC+4 (06670 -> AD 00117 NNADTEM) LOOP
//
// So loop is entered only when A is *negative* at 06664. Find out
// which path (negative value or negative zero), and trace where A
// gets its negative value.

#include "agc_harness.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"

#include <stdio.h>

extern agc_t *agc_core_state(void);
extern int    agc_engine(agc_t *State);

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    // Run to settled state.
    for (long c = 0; c < 500000; c++) agc_engine(st);

    printf("=== Full Z trace 300 cycles starting from cycle 500000 ===\n");
    printf("  cycle    Z    A     L     Q     FBANK NNADTEM NNTYPTEM\n");

    // Track which Z values are visited.
    static int visited[010000];
    for (long t = 0; t < 300; t++) {
        int z = st->Erasable[0][5] & 07777;
        int a = st->Erasable[0][0] & 077777;
        int l = st->Erasable[0][1] & 077777;
        int q = st->Erasable[0][2] & 077777;
        int fb = st->Erasable[0][4] & 077777;
        int nnad = st->Erasable[0][0117] & 077777;
        int nntp = st->Erasable[0][0120] & 077777;
        if (t < 60 || (t > 60 && t < 70))
            printf("  %5ld %05o %05o %05o %05o %05o   %05o   %05o\n",
                   500000 + t, z, a, l, q, fb, nnad, nntp);
        if (z >= 02000 && z < 010000) visited[z]++;
        agc_engine(st);
    }

    // Print unique addresses visited (the full loop body).
    printf("\n=== Unique Z addresses visited in 300-cycle window ===\n");
    int count = 0;
    int minz = -1, maxz = -1;
    for (int z = 0; z < 010000; z++) {
        if (visited[z]) {
            if (minz < 0) minz = z;
            maxz = z;
            count++;
        }
    }
    printf("  Count: %d distinct addresses, range %05o..%05o\n", count, minz, maxz);
    printf("  Visits:\n");
    for (int z = 0; z < 010000; z++) {
        if (visited[z])
            printf("    %05o: %d\n", z, visited[z]);
    }

    // Also dump the FBANK at this moment (the constants at 04xxx are
    // in fixed-fixed bank 2, but for clarity).
    printf("\nFinal RegFB=%05o RegEB=%05o RegBB=%05o\n",
           st->Erasable[0][4] & 077777, st->Erasable[0][3] & 077777,
           st->Erasable[0][6] & 077777);

    // Constants the loop reads: 04004, 04006, 04117, 05012, 06251, 04164,
    // 04741, 07742. Dump them.
    printf("\nFixed-fixed constants the loop reads:\n");
    int addrs[] = {04004, 04006, 04117, 04164, 04741, 05012, 06251, 07742};
    for (int i = 0; i < 8; i++) {
        int a = addrs[i];
        int word;
        if (a >= 06000) word = st->Fixed[3][a - 06000] & 077777;
        else            word = st->Fixed[2][a - 04000] & 077777;
        printf("  %05o = %05o\n", a, word);
    }

    PASS();
}
