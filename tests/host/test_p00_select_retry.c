// test_p00_select_retry — V35E first to put the AGC in a receptive
// state (per third_party/virtualagc/Contributed/LM_Simulator/doc/tutorial.txt
// and scenarios/LmQuickStart.txt), then V37E00E with retries.
//
// The reference Pi/Linux tutorial says: "Probably that has to be
// repeated a couple of times until PROG shows 00." So we retry up to
// MAX_RETRY times, asserting PRG=[0,0] at the end.

#include "agc_harness.h"
#include "test_helpers.h"
#include <stdio.h>

#define MAX_RETRY 8

static void snap_print(const char *tag) {
    dsky_state_t s;
    harness_snapshot(&s);
    printf("  %s: VRB=[%d,%d] PRG=[%d,%d] NUN=[%d,%d]\n", tag,
           s.verb[0], s.verb[1], s.prog[0], s.prog[1], s.noun[0], s.noun[1]);
}

static int prog_is_00(void) {
    dsky_state_t s;
    harness_snapshot(&s);
    return (s.prog[0] == 0 && s.prog[1] == 0);
}

int main(void)
{
    harness_boot();
    harness_step(200000);

    harness_post_key(15); harness_step(50000);  // RSET
    snap_print("after RSET");

    // V35E lamp test puts AGC into "receptive" state per LmQuickStart.txt
    harness_post_key(17); harness_step(100000); // V
    harness_post_key(3);  harness_step(100000);
    harness_post_key(5);  harness_step(100000);
    harness_post_key(28); harness_step(200000); // E
    snap_print("after V35E (lamp test active)");
    harness_step(3000000);
    snap_print("V35E + 3M cycles");

    // V37E00E with retries (per tutorial.txt: "repeat a couple of times")
    for (int attempt = 1; attempt <= MAX_RETRY; attempt++) {
        printf("--- attempt %d ---\n", attempt);
        harness_post_key(15); harness_step(50000);  // RSET
        harness_post_key(17); harness_step(100000); // V
        harness_post_key(3);  harness_step(100000);
        harness_post_key(7);  harness_step(100000);
        harness_post_key(28); harness_step(200000); // E
        harness_post_key(16); harness_step(100000); // 0
        harness_post_key(16); harness_step(100000); // 0
        harness_post_key(28); harness_step(500000); // final E
        snap_print("after V37E00E");
        // Give time for V37 to settle (sleep, rescue GOJAM, return to idle)
        harness_step(4000000);
        snap_print("+ 4M cycles");
        if (prog_is_00()) {
            printf("=== P00 selected on attempt %d ===\n", attempt);
            ASSERT(prog_is_00(), "PRG should be [0,0]");
            PASS();
        }
    }

    dsky_state_t s; harness_snapshot(&s);
    ASSERT(s.prog[0] == 0 && s.prog[1] == 0,
           "PRG never reached [0,0] — final PRG=[%d,%d]", s.prog[0], s.prog[1]);
    PASS();
}
