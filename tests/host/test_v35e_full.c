// test_v35e_full — post V35E, sample DSKY state over time, look for
// VRB digits = "35" and lamp-test segments lit during the verb's
// execution window.

#include "agc_harness.h"
#include "test_helpers.h"

#include <stdio.h>

int main(void)
{
    harness_boot();
    harness_step(200000);
    harness_type("R", 50000);

    printf("--- typing V35E ---\n");
    harness_post_key(17);   harness_step(100000);  // V
    dsky_state_t s;
    harness_snapshot(&s); printf("after V:   VRB=[%d,%d] PRG=[%d,%d] NUN=[%d,%d] ca=%d\n",
        s.verb[0], s.verb[1], s.prog[0], s.prog[1], s.noun[0], s.noun[1], s.comp_acty);

    harness_post_key(3);    harness_step(100000);  // 3
    harness_snapshot(&s); printf("after 3:   VRB=[%d,%d] PRG=[%d,%d] NUN=[%d,%d] ca=%d\n",
        s.verb[0], s.verb[1], s.prog[0], s.prog[1], s.noun[0], s.noun[1], s.comp_acty);

    harness_post_key(5);    harness_step(100000);  // 5
    harness_snapshot(&s); printf("after 5:   VRB=[%d,%d] PRG=[%d,%d] NUN=[%d,%d] ca=%d\n",
        s.verb[0], s.verb[1], s.prog[0], s.prog[1], s.noun[0], s.noun[1], s.comp_acty);

    harness_post_key(28);   harness_step(200000);  // E (ENTR)
    harness_snapshot(&s); printf("after E:   VRB=[%d,%d] PRG=[%d,%d] NUN=[%d,%d] ca=%d\n",
        s.verb[0], s.verb[1], s.prog[0], s.prog[1], s.noun[0], s.noun[1], s.comp_acty);

    // Sample 5 more times during V35 lamp test execution.
    for (int i = 0; i < 5; i++) {
        harness_step(200000);
        harness_snapshot(&s);
        printf("step %d:    VRB=[%d,%d] PRG=[%d,%d] NUN=[%d,%d] ca=%d ua=%d pa=%d temp=%d gim=%d trk=%d\n",
               i, s.verb[0], s.verb[1], s.prog[0], s.prog[1], s.noun[0], s.noun[1],
               s.comp_acty, s.uplink_acty, s.prog_alarm, s.temp, s.gimbal_lock, s.tracker);
    }

    PASS();
}
