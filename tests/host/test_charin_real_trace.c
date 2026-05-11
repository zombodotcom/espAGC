// test_charin_real_trace — find when AGC actually executes CHARIN.
// CHARIN is at bank 40 (octal) offset 02077. In yaAGC the engine
// needs FB shifted to 040 with the superbank bit set to map fixed
// memory bank 40 onto Z=02000-03777. We sample (Z, adjFB) and count
// only when adjFB == 040 AND Z is in CHARIN's range.

#include "agc_harness.h"
#include "test_helpers.h"
#include "yaAGC.h"
#include "agc_engine.h"
#include "dsky_keys.h"
#include <stdio.h>

extern agc_t *agc_core_state(void);
extern int agc_engine(agc_t *st);

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();
    for (long c = 0; c < 1000000; c++) agc_engine(st);

    int charin_hits = 0;
    int charin2_hits = 0;
    int verb_hits = 0;
    int rdspon_hits = 0;
    int prev_charin = 0;
    int z02077_any = 0;
    int unique_fb_seen[64] = {0};
    int unique_fb_count = 0;

    harness_post_key(DSKY_KEY_VERB);

    for (long c = 0; c < 200000; c++) {
        agc_engine(st);
        int z   = st->Erasable[0][5] & 07777;
        int fb  = st->Erasable[0][4] & 077777;
        int adjFB = (fb >> 10) & 037;
        // Superbank bit is in OutputChannel7 bit 6
        int sb  = (st->OutputChannel7 & 0100) ? 1 : 0;
        int physBank = (adjFB == 030 && sb) ? 040
                     : (adjFB == 031 && sb) ? 041
                     : (adjFB == 032 && sb) ? 042
                     : (adjFB == 033 && sb) ? 043
                     : adjFB;
        if (z == 02077) {
            z02077_any++;
            int key = (physBank << 1) | sb;
            if (key < 64 && !unique_fb_seen[key]) {
                unique_fb_seen[key] = 1;
                unique_fb_count++;
                if (unique_fb_count < 20)
                    printf("Z=02077 seen with adjFB=%02o physBank=%02o sb=%d\n",
                           adjFB, physBank, sb);
            }
        }
        if (physBank == 040) {
            if (z == 02077) {
                if (!prev_charin) {
                    charin_hits++;
                    if (charin_hits <= 5) {
                        int mpac0 = st->Erasable[0][0154] & 077777;
                        int cadrstor = st->Erasable[2][042] & 077777;
                        printf("CHARIN entry #%d: cycle=%ld MPAC0=%05o CADRSTOR=%05o\n",
                               charin_hits, c, mpac0, cadrstor);
                    }
                }
                prev_charin = 1;
            } else {
                prev_charin = 0;
            }
            if (z == 02112) charin2_hits++;
            if (z == 02354) verb_hits++;
        }
    }
    printf("\nIn 200k cycles after VERB key:\n");
    printf("  Z=02077 hits (any bank): %d\n", z02077_any);
    printf("  unique FB combos at Z=02077: %d\n", unique_fb_count);
    printf("  (FB=040 required for CHARIN):\n");
    printf("  CHARIN entries (Z=02077): %d\n", charin_hits);
    printf("  CHARIN2 hits  (Z=02112): %d\n", charin2_hits);
    printf("  VERB handler  (Z=02354): %d\n", verb_hits);
    PASS();
}
