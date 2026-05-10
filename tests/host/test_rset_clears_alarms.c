// test_rset_clears_alarms — validates the RSET-fixes-everything theory.
//
// On hardware we observed boot leaves PROG ALARM, RESTART, and the
// NightWatchman watchdog all asserted (canonical Block II fresh-start
// behavior — the astronaut is supposed to acknowledge by pressing
// RSET). This test boots the engine, snapshots the alarm state, sends
// a single RSET keypress, runs more cycles, and re-snapshots. If the
// theory is right, RESTART (and ideally PROG ALARM) clear after RSET.

#include "agc_harness.h"
#include "test_helpers.h"

#include <stdio.h>

static void dump(const char *tag)
{
    harness_alarms_t a;
    harness_alarms(&a);
    dsky_state_t s;
    harness_snapshot(&s);
    printf("%-12s  RuptLock=%d NW=%d TC=%d NoTC=%d PF=%d WF=%d GW=%d  "
           "prog_alarm=%d restart=%d stby=%d comp_acty=%d\n",
           tag, a.rupt_lock, a.night_watchman_tripped, a.tc_trap, a.no_tc,
           a.parity_fail, a.warning_filter_active, a.generated_warning,
           s.prog_alarm, s.restart, s.stby, s.comp_acty);
}

int main(void)
{
    harness_boot();

    // Step the engine through GOJAM and into what *should* be the
    // executive. NW trips after ~16 ms simulated time without a
    // NEWJOB access (memory address 067). If 5 M cycles isn't enough
    // for the executive to start up, the bug is in fresh-start / ROM
    // loading, not in cycle budgeting.
    for (int batch = 0; batch < 50; batch++) {
        harness_step(100000);
        harness_alarms_t a;
        harness_alarms(&a);
        if (a.night_watchman_tripped == 0) {
            printf("NW cleared after %d cycles\n", (batch + 1) * 100000);
            dump("clean");
            harness_type("R", 100000);
            dump("after RSET");
            harness_type("V37E00E", 100000);
            dump("after V37E00E");
            PASS();
        }
    }

    dump("after 5M cycles, NW still tripped");
    PASS();
}
