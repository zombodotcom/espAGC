// test_alarm_at_boot — boot Luminary099 with no peripherals, run for a
// while, dump every agc_t alarm flag and the resolved dsky_state.
//
// On hardware the boot log shows ch010 row=12 payload=0400 (= PROG caution
// bit) latched on. This test localizes whether that's driven by an
// agc_engine watchdog (NightWatchman / RuptLock / TCTrap / NoTC /
// ParityFail) — those would surface as set fields here — or whether
// it's the Luminary099 ROM itself raising a program-level caution
// because it can't see IMU/radar inputs (in which case all watchdog
// flags stay zero and only dsky_state.prog_alarm is set).
//
// This is investigation, not regression: the assertions here only check
// "engine ran without crashing" and "snapshot has plausible shape".
// The diagnostic is in the printed output.

#include "agc_harness.h"
#include "test_helpers.h"

#include <stdio.h>

#ifdef NEUTER_PSTUB
void peripheral_stub_tick(void *state) { (void)state; }
void peripheral_stub_init(void) {}
#endif

int main(void)
{
    harness_boot();
    // Run far past GOJAM completion. NightWatchman is transient (reset
    // every time AGC software touches address 067 = NEWJOB scheduler),
    // so a snapshot at the wrong instant can still show NW=1. Stepping
    // 5M cycles puts us deep into the executive where the watchdog is
    // settled.
    harness_step(5000000);

    harness_alarms_t a;
    harness_alarms(&a);
    dsky_state_t s;
    harness_snapshot(&s);

    printf("agc_t alarms: RuptLock=%d NW=%d TC=%d NoTC=%d PF=%d WF=%d GW=%d\n",
           a.rupt_lock, a.night_watchman_tripped, a.tc_trap, a.no_tc,
           a.parity_fail, a.warning_filter_active, a.generated_warning);

    printf("dsky_state:   prog_alarm=%d restart=%d stby=%d opr_err=%d "
           "key_rel=%d uplink_acty=%d temp=%d gimbal_lock=%d "
           "tracker=%d no_att=%d comp_acty=%d\n",
           s.prog_alarm, s.restart, s.stby, s.opr_err, s.key_rel,
           s.uplink_acty, s.temp, s.gimbal_lock, s.tracker, s.no_att,
           s.comp_acty);

    printf("digits: PRG=[%d,%d] VRB=[%d,%d] NUN=[%d,%d]\n",
           s.prog[0], s.prog[1], s.verb[0], s.verb[1], s.noun[0], s.noun[1]);

    ASSERT(s.generation > 0, "channel_router never recorded any output");
    PASS();
}
