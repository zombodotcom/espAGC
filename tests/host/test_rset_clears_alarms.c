// test_rset_clears_alarms — RSET keypress regression guard.
//
// On hardware boot leaves PROG ALARM, RESTART, and the NightWatchman
// watchdog all asserted (canonical Block II fresh-start behavior —
// the astronaut acknowledges by pressing RSET). This test boots the
// engine, waits for NightWatchman to settle, presses RSET, and asserts
// RESTART clears.
//
// Why only RESTART: the RESTART light flip-flop is hardware-direct —
// agc_engine.c WriteIO() clears State->RestartLight whenever ch015 is
// written with keycode 022 (RSET). Bypassing WriteIO when delivering
// keypresses (which the original channel_router did) leaves RESTART
// latched forever; routing through WriteIO is what makes this pass.
//
// PROG ALARM is a different story: it's Luminary software-asserted via
// the ALARM routine -> PROGLARM -> DSPTAB +11D bit 9 path, in response
// to peripheral fault bits Luminary still sees because we don't
// simulate IMU CDU counters / radar / AOT. RSET clears DSPTAB +11D and
// the IMODES30/33 fault bits, but Luminary immediately re-asserts them
// because the underlying condition (no IMU data) persists. That's a
// peripheral-simulation problem, not an RSET problem — so this test
// PRINTS the prog_alarm state for visibility but does not assert it.

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

            dsky_state_t before;
            harness_snapshot(&before);
            ASSERT(before.restart, "expected RESTART latched at fresh boot");

            harness_type("R", 100000);
            dump("after RSET");

            dsky_state_t after;
            harness_snapshot(&after);
            ASSERT(!after.restart,
                   "RSET keypress should clear RESTART (engine WriteIO path)");

            // V37E00E re-trips the warning filter because Luminary's P00
            // start path touches peripheral state we don't simulate. Run
            // it for visibility but don't gate the test on it.
            harness_type("V37E00E", 100000);
            dump("after V37E00E");
            PASS();
        }
    }

    dump("after 5M cycles, NW still tripped");
    ASSERT(0, "engine never left NightWatchman state — fresh-start broken");
}
