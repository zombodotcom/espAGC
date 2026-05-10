// test_lamp_test — V35E should light every status indicator briefly.
// Same investigation-not-regression pattern as test_p00_select.

#include "agc_harness.h"
#include "test_helpers.h"

#include <stdio.h>

int main(void)
{
    harness_boot();
    harness_step(200000);
    harness_type("R",    50000);   // RSET clears boot RESTART
    harness_type("V35E", 200000);

    dsky_state_t s;
    harness_snapshot(&s);
    printf("after V35E: comp_acty=%d uplink=%d prog_alarm=%d "
           "stby=%d restart=%d temp=%d gimbal=%d tracker=%d\n",
           s.comp_acty, s.uplink_acty, s.prog_alarm, s.stby,
           s.restart, s.temp, s.gimbal_lock, s.tracker);
    PASS();
}
