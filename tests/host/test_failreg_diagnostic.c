// test_failreg_diagnostic — boot Luminary099 with the current peripheral_stub
// wired in, step the engine for 10M cycles, dump FAILREG + alarms + dsky
// state after each batch. PASS unconditionally — this is a diagnostic capture,
// not a regression test.
//
// The point: find out *what alarm code* Luminary is actually firing, since
// every prior attempt at this watchdog was guessing. FAILREG[0] (erasable
// 0375 bank 0) holds the first alarm code seen since fresh-start. Per
// ALARM_AND_ABORT.agc:71-86, this is the value PROGLARM sees and lights the
// lamp from. Once we know the alarm code, we know which T4RUPT_PROGRAM.agc
// path is firing and which peripheral simulator pieces are required.
//
// Run output should be copied into docs/SESSION_NOTES.md.

#include "agc_harness.h"
#include "test_helpers.h"

#include <stdio.h>

int main(void)
{
    harness_boot();

    int batches = 100;            // 100 batches × 100k cycles = 10M total
    int cycles_per_batch = 100000;
    int prev_pa = -1;
    int prev_failreg0 = -1;
    int first_pa_set_at = -1;
    int first_failreg_set_at = -1;

    printf("# cycle    failreg[0,1,2]  pa res NW WF GW  cause-changed\n");

    for (int b = 0; b < batches; b++) {
        harness_step(cycles_per_batch);

        harness_failreg_t f;
        harness_failreg(&f);

        harness_alarms_t a;
        harness_alarms(&a);

        dsky_state_t s;
        harness_snapshot(&s);

        int total = (b + 1) * cycles_per_batch;

        bool change = (s.prog_alarm != prev_pa) || (f.latest != prev_failreg0);
        if (change) {
            printf("%9d  [%05o,%05o,%05o]  %d  %d  %d  %d  %d  %s\n",
                   total, f.latest, f.second, f.third,
                   s.prog_alarm, s.restart, a.night_watchman_tripped,
                   a.warning_filter_active, a.generated_warning,
                   change ? "*" : "");
        }
        if (s.prog_alarm && first_pa_set_at < 0)        first_pa_set_at = total;
        if (f.latest != 0 && first_failreg_set_at < 0)  first_failreg_set_at = total;

        prev_pa = s.prog_alarm;
        prev_failreg0 = f.latest;
    }

    // Final summary
    harness_failreg_t f;
    harness_failreg(&f);
    dsky_state_t s;
    harness_snapshot(&s);
    printf("---\n");
    printf("first prog_alarm=1 observed at cycle %d\n", first_pa_set_at);
    printf("first FAILREG[0] non-zero at cycle %d\n", first_failreg_set_at);
    printf("final FAILREG: [%05o,%05o,%05o] octal  (= [%d,%d,%d] decimal)\n",
           f.latest, f.second, f.third, f.latest, f.second, f.third);
    printf("final prog_alarm=%d restart=%d\n", s.prog_alarm, s.restart);

    PASS();
}
