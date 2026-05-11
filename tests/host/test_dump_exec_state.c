// test_dump_exec_state — raw dump of erasable bank 0 cells 0150..0250
// covering the entire executive workspace. Goal: see what's actually
// at slot 0 priority cell vs what we expect.
//
// Executive slot layout per CORE_PRIORITY_ASSIGNMENTS.agc:
//   First slot MPAC base at 0154. Each slot 014 (12 decimal) cells:
//     +0..+5  MPAC0..MPAC5  (job state)
//     +6      LOCCTR
//     +7      MODE
//     +8      LOC (job CADR)
//     +9      BANKSET
//     +10     PUSHLOC
//     +11     PRIORITY
// We sample the cells before/after each step of V/3/5/E.

#include "agc_harness.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"
#include "dsky_keys.h"

#include <stdio.h>

extern agc_t *agc_core_state(void);
extern int agc_engine(agc_t *st);

static void dump(const char *tag, agc_t *st)
{
    printf("\n=== %s ===\n", tag);
    printf("  IR5=%d ch015=%02o NEWJOB=%05o LOCCTR=%05o NEWPRIO=%05o\n",
           st->InterruptRequests[5],
           st->InputChannel[015] & 037,
           st->Erasable[0][0067] & 077777,
           st->Erasable[0][0070] & 077777,
           st->Erasable[0][0071] & 077777);
    printf("  Z=%05o B=%05o\n",
           st->Erasable[0][5] & 07777,
           st->Erasable[0][6] & 07777);
    // Dump bank 0 cells 0150..0250 (slots + waitlist + interp regs).
    printf("  exec workspace (bank 0):\n");
    for (int addr = 0150; addr < 0260; addr += 8) {
        printf("    %04o:", addr);
        for (int i = 0; i < 8 && addr+i < 0260; i++)
            printf(" %05o", st->Erasable[0][addr+i] & 077777);
        printf("\n");
    }
    // Decode each slot using documented +11 PRIORITY offset.
    printf("  slot decode (mpac_base = 0154 + N*014, PRIORITY at +11):\n");
    for (int s = 0; s < 8; s++) {
        int mpac_base = 0154 + s * 014;
        int prio = st->Erasable[0][mpac_base + 11] & 077777;
        int loc  = st->Erasable[0][mpac_base + 8]  & 077777;
        int bs   = st->Erasable[0][mpac_base + 9]  & 077777;
        printf("    slot %d @ base %04o : PRIO=%05o LOC=%05o BANK=%05o\n",
               s, mpac_base, prio, loc, bs);
    }
}

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();
    for (long c = 0; c < 1000000; c++) agc_engine(st);
    dump("1M cycles after boot", st);

    harness_post_key(DSKY_KEY_VERB);
    for (long c = 0; c < 5000; c++) agc_engine(st);
    dump("VERB +5k (early)", st);
    for (long c = 0; c < 295000; c++) agc_engine(st);
    dump("VERB +300k total", st);

    harness_post_key(3);
    for (long c = 0; c < 5000; c++) agc_engine(st);
    dump("'3' +5k (early)", st);
    for (long c = 0; c < 295000; c++) agc_engine(st);
    dump("'3' +300k total", st);

    harness_post_key(5);
    for (long c = 0; c < 300000; c++) agc_engine(st);
    dump("'5' +300k", st);

    harness_post_key(DSKY_KEY_ENTR);
    for (long c = 0; c < 5000; c++) agc_engine(st);
    dump("ENTR +5k (early)", st);
    for (long c = 0; c < 995000; c++) agc_engine(st);
    dump("ENTR +1M total", st);

    PASS();
}
