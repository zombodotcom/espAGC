// test_executive_state — dump Luminary's executive scheduler state
// (PRIORITY array, NEWJOB, LOC, DSPLOCK) right after a keypress to
// determine whether NOVAC is actually scheduling CHARIN. If NOVAC
// works, one PRIORITY slot should hold CHRPRIO (30000 octal); if
// NEWJOB picks it, the dispatch should occur.
//
// Addresses (from MAIN.agc.html symtab):
//   NEWJOB  @ 0067  -> bank 0 offset 067
//   PRIORITY @ 0167 -> bank 0 offset 0167, array of 7 (one per core set)
//   LOC      @ 0164 -> bank 0 offset 0164
//   FIXLOC   @ 0120 -> bank 0 offset 0120
//   DSPLOCK  @ 01012 -> bank 2 offset 012

#include "agc_harness.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"

#include <stdio.h>

extern agc_t *agc_core_state(void);

static void dump_state(agc_t *st, const char *tag)
{
    printf("=== %s ===\n", tag);
    printf("  RegZ=%06o cyc_lo=%u Standby=%d RestartLight=%d\n",
           st->Erasable[0][RegZ], (unsigned)(st->CycleCounter & 0xFFFFFFFFu),
           st->Standby, st->RestartLight);
    printf("  NEWJOB=%06o  LOC=%06o  FIXLOC=%06o\n",
           st->Erasable[0][0067], st->Erasable[0][0164], st->Erasable[0][0120]);
    // PRIORITY is 7 entries with stride 014 octal (12 decimal) per core set
    printf("  PRIORITY (12-word stride) = [%06o %06o %06o %06o %06o %06o %06o]\n",
           st->Erasable[0][0167 + 0*014], st->Erasable[0][0167 + 1*014],
           st->Erasable[0][0167 + 2*014], st->Erasable[0][0167 + 3*014],
           st->Erasable[0][0167 + 4*014], st->Erasable[0][0167 + 5*014],
           st->Erasable[0][0167 + 6*014]);
    printf("  ch015=%06o IntReq[5]=%d AllowInterrupt=%d InIsr=%d ExtraDelay=%d\n",
           st->InputChannel[015], st->InterruptRequests[5],
           st->AllowInterrupt, st->InIsr, st->ExtraDelay);
    printf("  FAILREG=[%06o,%06o,%06o]  DSPLOCK=%06o\n",
           st->Erasable[0][0375], st->Erasable[0][0376], st->Erasable[0][0377],
           st->Erasable[2][012]);
}

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    harness_step(200000);
    dump_state(st, "after 200k cycles (post-boot, peripheral_stub cleared NW)");

    harness_post_key(18);   // RSET
    harness_step(1000);
    dump_state(st, "+1000 cycles after RSET post");

    harness_step(10000);
    dump_state(st, "+11000 cycles after RSET post");

    harness_step(100000);
    dump_state(st, "+111000 cycles after RSET post");

    PASS();
}
