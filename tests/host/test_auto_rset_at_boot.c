// test_auto_rset_at_boot — verifies that the channel_router posts a
// synthetic DSKY_KEY_RSET keypress after ~16 ChannelRoutine ticks at
// boot. This is option (a) from docs/superpowers/specs/2026-05-10-
// prog-alarm-watchdog-design.md: the initial flush that clears PROG
// ALARM's latch via the engine's hardware-direct RSET path.
//
// The RSET-cleared RestartLight state is transient — Luminary's
// GOJAM re-asserts within ~1000 cycles because peripheral state is
// still missing (option b's job, in Tasks 4-7). So we poll in fine
// batches and accept any single observation of restart=0 as proof
// the auto-RSET fired and routed through WriteIO(ch015, RSET).

#include "agc_harness.h"
#include "test_helpers.h"

#include <stdio.h>

int main(void)
{
    harness_boot();

    // Run past the auto-RSET threshold (tick 16 = ~131k cycles), then
    // poll in 500-cycle batches looking for the brief restart=0 window.
    // The empirical window is ~1000 cycles wide before Luminary's
    // GOJAM re-asserts, so 500-cycle granularity reliably catches it.
    harness_step(120000);

    for (int i = 0; i < 200; i++) {
        dsky_state_t s;
        harness_snapshot(&s);
        if (!s.restart) {
            printf("restart=0 observed at +%d cycles after threshold "
                   "(prog_alarm=%d) — auto-RSET fired correctly\n",
                   i * 500, s.prog_alarm);
            PASS();
        }
        harness_step(500);
    }

    ASSERT(0, "restart never cleared in 100k cycles after threshold — "
              "auto-RSET did not fire (or did not route through WriteIO)");
}
