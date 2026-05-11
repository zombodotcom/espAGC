// io_callbacks.c
//
// Provides the API surface the yaAGC engine expects from a host integration
// (yaAGC normally uses SocketAPI.c to relay IO over TCP). On ESP32 we instead
// route every output channel write into channel_router and pull pending input
// events out of channel_router on every engine call.

#include "yaAGC.h"
#include "agc_engine.h"

#include "channel_router.h"

#ifdef CONFIG_AGC_TRACE_KEYRUPT1
#include "esp_log.h"
static const char *KEYRUPT_TAG = "keyrupt";

// KEYRUPT1 lives at fixed-fixed 04024 and runs through 04046 (TC RESUME).
// The interesting window is the full KEYRUPT1 body plus the first instr
// of NOVAC. See tests/host/test_cadr_resolution.c header for the byte-by
// -byte counting. Bank 2 fixed-fixed covers addresses 04000..05777,
// indexed as State->Fixed[2][addr - 04000].
#define KEYRUPT_LO 04024
#define KEYRUPT_HI 04046

static void keyrupt_trace_step(agc_t *State)
{
    int z = State->Erasable[0][RegZ];
    static bool in_window = false;
    if (z < KEYRUPT_LO || z > KEYRUPT_HI) {
        in_window = false;
        return;
    }
    // Re-arm: we want to see EVERY instruction inside the window, not
    // just the first one. The latch only suppresses re-entry chatter on
    // the SAME instruction across non-stepping calls (ChannelInput can
    // be called more than once per actual engine step depending on
    // engine internals).
    (void)in_window;
    in_window = true;

    int a    = State->Erasable[0][RegA] & 077777;
    int l    = State->Erasable[0][RegL] & 077777;
    int q    = State->Erasable[0][RegQ] & 077777;
    int fb   = State->InputChannel[004] & 077777;
    int bb   = State->InputChannel[006] & 077777;
    int isr  = State->InIsr;
    int ec   = State->ExtraCode;

    // Decode Q -> a Fixed[] cell. In fixed-fixed (04000-07777), Q indexes
    // Fixed[2] or Fixed[3] regardless of FBANK. Otherwise FBANK selects
    // the bank (bits 14:10 of FBANK channel register). Be defensive: if
    // Q is out of range, log zeros for word_q/word_q1 rather than reading
    // out of bounds.
    int word_q = 0, word_q1 = 0;
    if (q >= 04000 && q <= 05777) {
        word_q  = State->Fixed[2][q     - 04000] & 077777;
        word_q1 = State->Fixed[2][(q+1) - 04000] & 077777;
    } else if (q >= 06000 && q <= 07777) {
        word_q  = State->Fixed[3][q     - 06000] & 077777;
        word_q1 = State->Fixed[3][(q+1) - 06000] & 077777;
    } else if (q >= 02000 && q <= 03777) {
        int bank = (fb >> 10) & 037;
        if (bank < 40) {
            word_q  = State->Fixed[bank][q     - 02000] & 077777;
            word_q1 = State->Fixed[bank][(q+1) - 02000] & 077777;
        }
    }

    ESP_LOGI(KEYRUPT_TAG,
             "Z=%05o FB=%05o BB=%05o A=%05o L=%05o Q=%05o isr=%d ec=%d "
             "[Q]=%06o [Q+1]=%06o",
             z, fb, bb, a, l, q, isr, ec, word_q, word_q1);
}
#endif

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
#ifdef CONFIG_AGC_TRACE_KEYRUPT1
    keyrupt_trace_step(State);
#endif
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
