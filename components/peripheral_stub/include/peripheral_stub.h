#pragma once
// peripheral_stub — port of LM_Simulator's behaviour into our integration.
//
// On Pi/Linux, LM_Simulator is a separate Tcl process that talks to
// yaAGC over a TCP socket. It writes channels 30/31/32/33 at startup
// AND periodically thereafter, pushes CDU counter pulses via the
// engine's UnprogrammedSequence mechanism, and responds to AGC outputs
// (jet firings, throttle) by integrating simulated spacecraft dynamics.
//
// We collapse that separate process into in-process code that uses the
// engine's *same* WriteIO and UnprogrammedIncrement entry points — so
// the AGC sees identical semantics whether running on Pi or ESP32.
//
// Functions:
//   peripheral_stub_init()           — initial channel writes + state setup.
//   peripheral_stub_step(state, dt)  — one simulation step (call at 100 Hz).
//   peripheral_stub_tick(state)      — legacy hook from
//                                       channel_router_on_routine();
//                                       does the IMODES freshness + the
//                                       host-side ERROR-on-NW carve-out.
//                                       Kept for backwards compatibility;
//                                       the *real* simulation work moves
//                                       to peripheral_stub_step.

#include "yaAGC.h"
#include "agc_engine.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void peripheral_stub_init(void);
void peripheral_stub_step(agc_t *state, uint32_t dt_us);
void peripheral_stub_tick(agc_t *state);

#ifdef __cplusplus
}
#endif
