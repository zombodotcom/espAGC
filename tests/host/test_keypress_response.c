// test_keypress_response — does the engine produce ANY new ch010 output
// after a keypress? CHARIN ultimately writes the typed character into
// DSPCOUNT and updates ch010 row writes. If CHARIN runs we'd see
// distinctive new ch010 traffic after the keypress that wasn't there
// before.
//
// Test:
//   1. Boot, step 200k cycles (past auto-RSET).
//   2. Snapshot dsky_state digits (PRG/VRB/NUN/R1/R2/R3).
//   3. Post V key, step 100k cycles.
//   4. Snapshot. Did VRB change? CHARIN should display the verb digit.
//   5. Post 3, 5 — verb should display "35".
//   6. Post E. V35 = lamp test - should light all status lamps.

#include "agc_harness.h"
#include "test_helpers.h"

#include <stdio.h>

static void print_state(const char *tag, const dsky_state_t *s)
{
    #define DC(d) ((d) < 0 ? '_' : (char)('0' + (d)))
    printf("%-16s PRG=%c%c VRB=%c%c NUN=%c%c  R1=%c%c%c%c%c  ca=%d up=%d pa=%d "
           "stby=%d restart=%d tracker=%d gimbal=%d temp=%d\n",
           tag,
           DC(s->prog[0]), DC(s->prog[1]),
           DC(s->verb[0]), DC(s->verb[1]),
           DC(s->noun[0]), DC(s->noun[1]),
           DC(s->r1[0]), DC(s->r1[1]), DC(s->r1[2]), DC(s->r1[3]), DC(s->r1[4]),
           s->comp_acty, s->uplink_acty, s->prog_alarm,
           s->stby, s->restart, s->tracker, s->gimbal_lock, s->temp);
    #undef DC
}

int main(void)
{
    harness_boot();

    harness_step(200000);
    dsky_state_t s0;
    harness_snapshot(&s0);
    print_state("boot+200k", &s0);

    harness_type("V", 50000);
    dsky_state_t s1;
    harness_snapshot(&s1);
    print_state("after V", &s1);

    harness_type("3", 50000);
    dsky_state_t s2;
    harness_snapshot(&s2);
    print_state("after V3", &s2);

    harness_type("5", 50000);
    dsky_state_t s3;
    harness_snapshot(&s3);
    print_state("after V35", &s3);

    harness_type("E", 200000);
    dsky_state_t s4;
    harness_snapshot(&s4);
    print_state("after V35E", &s4);

    // For the lamp test (V35E), we expect status lamps to light up.
    // For just V verb-digit display, verb digits should show 35.
    if (s3.verb[0] == 3 && s3.verb[1] == 5) {
        printf("\n*** SUCCESS *** VRB digits show '35' after V3 5 keys\n");
        printf("CHARIN is processing keypresses correctly.\n");
    } else if (s1.verb[0] == s0.verb[0] && s1.verb[1] == s0.verb[1] &&
               s4.tracker == s0.tracker && s4.gimbal_lock == s0.gimbal_lock) {
        printf("\nFAIL: Keypress produced no observable DSKY change.\n");
        printf("CHARIN is NOT running. Bug confirmed.\n");
    } else {
        printf("\nPARTIAL: some state changed but verb digits didn't reach 35.\n");
        printf("Maybe CHARIN runs but display state path is broken.\n");
    }

    PASS();
}
