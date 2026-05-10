// test_auto_rset_at_boot — verifies that the channel_router posts a
// synthetic DSKY_KEY_RSET keypress after ~50 ChannelRoutine ticks at
// boot. This is option (a) from docs/superpowers/specs/2026-05-10-
// prog-alarm-watchdog-design.md: the initial flush that clears PROG
// ALARM's latch via the engine's hardware-direct RSET path.

#include "agc_harness.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"

#include <stdio.h>

extern agc_t *agc_core_state(void);

int main(void)
{
    harness_boot();

    // Step the engine long enough that channel_router_on_routine() has
    // been called >= 50 times. The engine calls ChannelRoutine every
    // 02000 cycles, so 50 * 02000 = 100000 cycles is the floor. Add a
    // ~5x margin and post-RSET consume room.
    harness_step(500000);

    // After auto-RSET fires, RSET keycode (18) should have been routed
    // through WriteIO(015, ...) — which clears RestartLight in the
    // engine. We assert restart lamp is clear as the observable side
    // effect.
    dsky_state_t s;
    harness_snapshot(&s);
    printf("post-boot: restart=%d prog_alarm=%d\n", s.restart, s.prog_alarm);

    ASSERT(!s.restart,
        "auto-RSET should have flushed RestartLight via WriteIO(ch015, RSET)");

    PASS();
}
