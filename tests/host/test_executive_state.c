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
    // FBANK is at I/O channel 04; BBANK at 06; SBANK at 07 (super-bank).
    // (See agc_engine.h channel register definitions.)
    int fbank   = st->InputChannel[004];
    int bbank   = st->InputChannel[006];
    int sbank   = st->InputChannel[007];

    printf("=== %s ===\n", tag);
    printf("  RegZ=%06o FBANK=%06o BBANK=%06o SBANK=%06o cyc=%u\n",
           st->Erasable[0][RegZ], fbank, bbank, sbank,
           (unsigned)(st->CycleCounter & 0xFFFFFFFFu));
    printf("  Standby=%d RestartLight=%d AllowInt=%d InIsr=%d ExtraDelay=%d\n",
           st->Standby, st->RestartLight, st->AllowInterrupt,
           st->InIsr, st->ExtraDelay);
    printf("  NEWJOB=%06o  LOC=%06o  FIXLOC=%06o BANKSET=%06o\n",
           st->Erasable[0][0067], st->Erasable[0][0164],
           st->Erasable[0][0120], st->Erasable[0][0165]);
    // PRIORITY: 7 entries x 12-word stride starting at 0167 (bank 0).
    // CADR (entry-point reference) lives at PRIORITY+1, one cell after each priority.
    for (int slot = 0; slot < 7; slot++) {
        int p = st->Erasable[0][0167 + slot*014];
        int cadr = st->Erasable[0][0170 + slot*014];
        printf("  slot%d: PRIO=%06o CADR=%06o (FBANK%07o:%05o)\n",
               slot, p, cadr, (cadr >> 10) & 037, cadr & 01777);
    }
    // Interpreter state cells: POLISH at 0117 holds the current
    // indirection target; BANKSET at 0165 holds the saved BBANK for
    // returning from indirect resolves. If POLISH cycles back to itself
    // (or to a cell that points back), the GOTO indirection loop spins.
    printf("  POLISH=%06o BANKSET=%06o ADDRWD=%06o MPAC=%06o..%06o\n",
           st->Erasable[0][0117], st->Erasable[0][0165], st->Erasable[0][0116],
           st->Erasable[0][0154], st->Erasable[0][0162]);
    printf("  ch015=%06o IntReq[5]=%d  FAILREG=[%06o,%06o,%06o] DSPLOCK=%06o\n",
           st->InputChannel[015], st->InterruptRequests[5],
           st->Erasable[0][0375], st->Erasable[0][0376], st->Erasable[0][0377],
           st->Erasable[2][012]);
}

int main(void)
{
    // Step JUST PAST tick 16 (=131072 cycles) — but BEFORE the auto-RSET
    // fires. We're going to step in fine-grained increments and check
    // state, then post a MANUAL keypress and see if it actually
    // reaches CHARIN's first instruction (DSPLOCK 0->1 transition).
    harness_boot();
    agc_t *st = agc_core_state();

    // Step to JUST BEFORE auto-RSET (tick 16 fires around cycle 131072).
    // Stop at 125000 to give ourselves a window before it fires.
    harness_step(125000);
    dump_state(st, "cycle 125k (BEFORE auto-RSET fire)");

    // Now post a manual RSET. Step a few thousand cycles to let CHARIN
    // be scheduled and (hopefully) run.
    harness_post_key(18);   // RSET
    harness_step(2000);
    dump_state(st, "+2k cycles after manual RSET (pre-auto-RSET window)");

    harness_step(20000);
    dump_state(st, "+22k cycles after manual RSET");

    PASS();
}
