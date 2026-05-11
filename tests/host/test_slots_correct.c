// test_slots_correct — dump executive slot cells using CORRECT offsets.
//
// Slot layout (from ERASABLE_ASSIGNMENTS.agc:388-395):
//   MPAC ERASE +6   ->  7 cells at offset 0..6
//   MODE ERASE      ->  offset 7
//   LOC ERASE       ->  offset 8
//   BANKSET ERASE   ->  offset 9
//   PUSHLOC ERASE   ->  offset 10
//   PRIORITY ERASE  ->  offset 11 (LAST cell of slot)
//
// PRIORITY[slot 0] is at erasable address 0167 (referenced as `0167`
// in test_executive_state.c). That means MPAC[slot 0] = 0167 - 11 = 0154.
//
// For slot N:
//   MPAC[N]     = 0154 + N*014
//   MODE[N]     = 0163 + N*014
//   LOC[N]      = 0164 + N*014
//   BANKSET[N]  = 0165 + N*014
//   PUSHLOC[N]  = 0166 + N*014
//   PRIORITY[N] = 0167 + N*014
//
// Post a series of keypresses and observe whether NOVAC's slot
// allocation actually lands LOC=02077, BANKSET=60101 (the 2CADR CHARIN
// words confirmed by test_find_keyrupt1.c).

#include "agc_harness.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"
#include "dsky_keys.h"

#include <stdio.h>

extern agc_t *agc_core_state(void);

#define DSPLOCK_BANK 2
#define DSPLOCK_OFF  012

static void dump(const char *tag, agc_t *st)
{
    int ir5     = st->InterruptRequests[5];
    int ch015   = st->InputChannel[015] & 037;
    int newjob  = st->Erasable[0][0067] & 077777;
    int dsplock = st->Erasable[DSPLOCK_BANK][DSPLOCK_OFF] & 077777;
    printf("%-25s IR5=%d ch015=%02o NEWJOB=%05o DSPLOCK=%05o\n",
           tag, ir5, ch015, newjob, dsplock);
    printf("  slot [PRIO  LOC   BANK  MODE  PUSH  MPAC0]:\n");
    for (int s = 0; s < 8; s++) {
        int mpac_base = 0154 + s * 014;
        int prio = st->Erasable[0][mpac_base + 11] & 077777;
        int loc  = st->Erasable[0][mpac_base + 8]  & 077777;
        int bs   = st->Erasable[0][mpac_base + 9]  & 077777;
        int mode = st->Erasable[0][mpac_base + 7]  & 077777;
        int push = st->Erasable[0][mpac_base + 10] & 077777;
        int mpac0= st->Erasable[0][mpac_base + 0]  & 077777;
        if (prio || loc || bs || mode || mpac0)
            printf("    [%d] %05o %05o %05o %05o %05o %05o\n",
                   s, prio, loc, bs, mode, push, mpac0);
    }
}

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    // Run past initial boot transient + auto-RSET.
    for (long c = 0; c < 1000000; c++) {
        extern int agc_engine(agc_t *);
        agc_engine(st);
    }
    dump("1M cycles (settled)", st);

    extern int agc_engine(agc_t *);

    harness_post_key(DSKY_KEY_VERB);
    for (long c = 0; c < 300000; c++) agc_engine(st);
    dump("After VERB +300k", st);

    harness_post_key(3);
    for (long c = 0; c < 300000; c++) agc_engine(st);
    dump("After 3 +300k", st);

    harness_post_key(5);
    for (long c = 0; c < 300000; c++) agc_engine(st);
    dump("After 5 +300k", st);

    harness_post_key(DSKY_KEY_ENTR);
    for (long c = 0; c < 500000; c++) agc_engine(st);
    dump("After ENTR +500k", st);

    // Expectation: each keypress should land LOC=02077, BANKSET=60101
    // into a fresh slot. If instead we see LOC=0 or any other value,
    // NOVAC/SETLOC isn't storing the 2CADR correctly.

    PASS();
}
