// test_v37e00e_full — step-by-step V37E00E (select P00) verification.

#include "agc_harness.h"
#include "test_helpers.h"
#include <stdio.h>

int main(void)
{
    harness_boot();
    harness_step(200000);
    harness_type("R", 50000);

    dsky_state_t s;

    printf("--- typing V37E00E ---\n");
    harness_post_key(17);   harness_step(150000);  // V
    harness_snapshot(&s); printf("after V: VRB=[%d,%d] PRG=[%d,%d] NUN=[%d,%d]\n",
        s.verb[0], s.verb[1], s.prog[0], s.prog[1], s.noun[0], s.noun[1]);

    harness_post_key(3);    harness_step(150000);
    harness_snapshot(&s); printf("after 3: VRB=[%d,%d] PRG=[%d,%d] NUN=[%d,%d]\n",
        s.verb[0], s.verb[1], s.prog[0], s.prog[1], s.noun[0], s.noun[1]);

    harness_post_key(7);    harness_step(150000);
    harness_snapshot(&s); printf("after 7: VRB=[%d,%d] PRG=[%d,%d] NUN=[%d,%d]\n",
        s.verb[0], s.verb[1], s.prog[0], s.prog[1], s.noun[0], s.noun[1]);

    harness_post_key(28);   harness_step(200000);  // E
    harness_snapshot(&s); printf("after E: VRB=[%d,%d] PRG=[%d,%d] NUN=[%d,%d]\n",
        s.verb[0], s.verb[1], s.prog[0], s.prog[1], s.noun[0], s.noun[1]);

    harness_post_key(0);    harness_step(150000);  // 0
    harness_snapshot(&s); printf("after 0: VRB=[%d,%d] PRG=[%d,%d] NUN=[%d,%d]\n",
        s.verb[0], s.verb[1], s.prog[0], s.prog[1], s.noun[0], s.noun[1]);

    harness_post_key(0);    harness_step(150000);  // 0
    harness_snapshot(&s); printf("after 0: VRB=[%d,%d] PRG=[%d,%d] NUN=[%d,%d]\n",
        s.verb[0], s.verb[1], s.prog[0], s.prog[1], s.noun[0], s.noun[1]);

    harness_post_key(28);   harness_step(500000);  // final E
    harness_snapshot(&s); printf("after E: VRB=[%d,%d] PRG=[%d,%d] NUN=[%d,%d]\n",
        s.verb[0], s.verb[1], s.prog[0], s.prog[1], s.noun[0], s.noun[1]);

    for (int i = 0; i < 10; i++) {
        harness_step(500000);
        harness_snapshot(&s);
        printf("step %d: VRB=[%d,%d] PRG=[%d,%d] NUN=[%d,%d]\n", i,
            s.verb[0], s.verb[1], s.prog[0], s.prog[1], s.noun[0], s.noun[1]);
    }

    PASS();
}
