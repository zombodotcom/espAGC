// io_callbacks.c
//
// Provides the API surface the yaAGC engine expects from a host integration
// (yaAGC normally uses SocketAPI.c to relay IO over TCP). On ESP32 we instead
// route every output channel write into channel_router and pull pending input
// events out of channel_router on every engine call.

#include "yaAGC.h"
#include "agc_engine.h"

#include "channel_router.h"

// ---------------------------------------------------------------------------
// Output: engine writes to a channel.
void ChannelOutput(agc_t *State, int Channel, int Value)
{
    (void)State;
    channel_router_on_output(Channel, Value);
}

// ---------------------------------------------------------------------------
// Input: engine polls each step. Drain pending events from the router into the
// AGC state, raising interrupt flags as appropriate.
int ChannelInput(agc_t *State)
{
    return channel_router_pump_input(State);
}

// ---------------------------------------------------------------------------
// Periodic housekeeping; called on every engine cycle modulo a small counter.
// We keep it cheap and let channel_router decide what to do.
void ChannelRoutine(agc_t *State)
{
    (void)State;
    channel_router_on_routine();
}

// ---------------------------------------------------------------------------
// LM-only DEDA hook. Apollo 11 LM uses the AGC, not the AGS, so this is a
// stub: the engine still references it for code paths that exist in shared
// utilities.
void ShiftToDeda(agc_t *State, int Data)
{
    (void)State;
    (void)Data;
}

// ---------------------------------------------------------------------------
// Radar data injection point. Without simulated radar we return without
// updating RNRAD; the engine will simply see no fresh radar samples.
void RequestRadarData(agc_t *State)
{
    (void)State;
}
