// test_lm_idle_quiet — contract test: with peripheral_stub wired in,
// PROG ALARM must clear within ~1M cycles of boot and stay clear
// across at least 5M total cycles of Luminary idle.
//
// Background: the FAILREG diagnostic established the only alarm fired
// is `01107 NIGHT WATCHMAN`, set once during the boot-time transient
// because Luminary's executive doesn't reach NEWJOB (address 067) fast
// enough during the first SCALER1 cycle. The engine GOJAMs, recovers,
// and the executive runs normally — but `FAILREG[0]` and `DSPTAB+11D`
// bit 9 retain the trip. Luminary's ERROR routine on RSET is supposed
// to zero both, but our KEYRUPT1 → NOVAC(CHARIN) path isn't reaching
// CHARIN (DSPLOCK never transitions). Until that's understood,
// `peripheral_stub_tick` does what ERROR does, from outside the engine.
//
// This test is the contract: idle Luminary, PROG ALARM clear and stable.

#include "agc_harness.h"
#include "test_helpers.h"

#include <stdio.h>

int main(void)
{
    harness_boot();

    int cycles_per_batch = 100000;
    int settle_batches = 20;       // 2M cycles to clear initial NW transient
    int hold_batches = 50;         // additional 5M cycles to verify staying clear
    int prog_alarm_set_count = 0;
    int last_prog_alarm = -1;
    int last_prog_alarm_set_cycle = -1;

    // Phase 1: settle. Let the engine boot, NW trip & recover, and
    // peripheral_stub clear the alarm.
    for (int b = 0; b < settle_batches; b++) {
        harness_step(cycles_per_batch);
    }

    dsky_state_t s;
    harness_snapshot(&s);
    harness_failreg_t f;
    harness_failreg(&f);
    printf("after %dM cycles settle: prog_alarm=%d  FAILREG=[%05o,%05o,%05o]\n",
           settle_batches * cycles_per_batch / 1000000,
           s.prog_alarm, f.latest, f.second, f.third);

    ASSERT(!s.prog_alarm,
        "prog_alarm should be cleared after settle period (peripheral_stub host-side ERROR not running?)");

    // Phase 2: hold. Continue stepping and verify pa never goes back to 1.
    for (int b = 0; b < hold_batches; b++) {
        harness_step(cycles_per_batch);
        harness_snapshot(&s);
        if (s.prog_alarm) {
            if (last_prog_alarm == 0) {
                printf("prog_alarm RE-LATCHED at hold batch %d (total cycle %d)\n",
                       b, (settle_batches + b + 1) * cycles_per_batch);
                last_prog_alarm_set_cycle = (settle_batches + b + 1) * cycles_per_batch;
            }
            prog_alarm_set_count++;
        }
        last_prog_alarm = s.prog_alarm;
    }

    harness_snapshot(&s);
    harness_failreg(&f);
    printf("after total %dM cycles: prog_alarm=%d  FAILREG=[%05o,%05o,%05o]\n",
           (settle_batches + hold_batches) * cycles_per_batch / 1000000,
           s.prog_alarm, f.latest, f.second, f.third);
    printf("prog_alarm_set_count during hold phase: %d (of %d batches)\n",
           prog_alarm_set_count, hold_batches);

    ASSERT(prog_alarm_set_count == 0,
        "prog_alarm should stay clear through 5M-cycle hold phase");

    PASS();
}
