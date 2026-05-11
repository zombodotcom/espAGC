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
static const char *DISP_TAG    = "disp";

// Dispatcher trace: on every ChannelInput cycle, watch a handful of
// engine state cells for transitions and emit one log line when any of
// them change. Targeted at answering "does KEYRUPT1 ever get a chance
// to dispatch?" by showing the moment InIsr goes 1->0 and what
// InterruptRequests[] looks like at that moment. Rate-limited by
// transition-only logging - at the observed ~60 ISR entries/sec the
// log rate stays well under the UART budget.
static void dispatch_trace_step(agc_t *State)
{
    static int prev_isr      = -1;
    static int prev_allow    = -1;
    static int prev_reqs[11] = { -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1 };

    int isr   = State->InIsr;
    int allow = State->AllowInterrupt;
    int z     = State->Erasable[0][RegZ];

    bool changed = (isr != prev_isr) || (allow != prev_allow);
    for (int i = 1; i <= 10; i++) {
        if (State->InterruptRequests[i] != prev_reqs[i]) changed = true;
    }
    if (!changed) return;

    char reqs[11];
    for (int i = 1; i <= 10; i++) {
        reqs[i - 1] = State->InterruptRequests[i] ? '1' : '0';
    }
    reqs[10] = 0;
    ESP_LOGI(DISP_TAG,
             "Z=%05o isr=%d AI=%d reqs[1..10]=%s",
             z, isr, allow, reqs);

    prev_isr   = isr;
    prev_allow = allow;
    for (int i = 1; i <= 10; i++) prev_reqs[i] = State->InterruptRequests[i];
}

// KEYRUPT1 lead-in lives at fixed-fixed 04024 (per
// Luminary099/INTERRUPT_LEAD_INS.agc:60-63). After the `TCF KEYRUPT1`
// at 04027, control jumps to the actual handler in switched-bank
// fixed memory — outside the 04024..04046 lead-in window. To follow
// the full handler → NOVAC → NOVAC2 → CHARIN(?) chain we latch on
// ISR-entry-at-04024 and log every instruction until InIsr drops
// back to 0 (RESUME). Per-instruction dedup (compare RegZ to last)
// prevents the engine's "ChannelInput called multiple times per
// instruction" pattern from flooding UART. Expected: 50-300 lines
// per keypress.
//
// Watched erasable cells (one-shot deltas printed when they change).
// Per Luminary099/ERASABLE_ASSIGNMENTS.agc:388-395, each executive
// "job slot" is 12 words: MPAC..MPAC+6 (7) + MODE + LOC + BANKSET +
// PUSHLOC + PRIORITY. So PRIORITY is at offset 11 of the slot (the
// LAST cell), and the job's entry address ("CADR") is in LOC at
// offset 8. Slot 0 starts at erasable[0][0154] (MPAC[0]).
//
//   DSPLOCK   @ bank 2 offset 012  — CHARIN's `XCH DSPLOCK` sets it
//   LOC[0]    @ bank 0 offset 0163 — slot 0's job entry address (CADR)
//   PRIO[0]   @ bank 0 offset 0167 — slot 0's job priority
static void keyrupt_trace_step(agc_t *State)
{
    static bool following     = false;
    static int  last_z        = -1;
    static int  last_dsplock  = -1;
    static int  last_prio0    = -1;
    static int  last_loc0     = -1;

    int z   = State->Erasable[0][RegZ];
    int isr = State->InIsr;

    // ENTRY: ISR running at KEYRUPT1 lead-in entry. Dump the full
    // slot-0 cell block (MPAC..PRIORITY at 0154..0167) so we can see
    // what's actually scheduled there.
    if (!following && isr && z == 04024) {
        following     = true;
        last_z        = -1;
        last_dsplock  = State->Erasable[2][012];
        last_prio0    = State->Erasable[0][0167];
        last_loc0     = State->Erasable[0][0163];
        ESP_LOGI(KEYRUPT_TAG,
                 "==KEYRUPT1 ENTRY== ch015=%05o DSPLOCK=%05o",
                 State->InputChannel[015] & 077777,
                 last_dsplock & 077777);
        ESP_LOGI(KEYRUPT_TAG,
                 "  slot0: MODE=%05o LOC=%05o BANKSET=%05o PUSHLOC=%05o PRIORITY=%05o",
                 State->Erasable[0][0162] & 077777,
                 State->Erasable[0][0163] & 077777,
                 State->Erasable[0][0164] & 077777,
                 State->Erasable[0][0165] & 077777,
                 State->Erasable[0][0167] & 077777);
        ESP_LOGI(KEYRUPT_TAG,
                 "  slot1: MODE=%05o LOC=%05o BANKSET=%05o PUSHLOC=%05o PRIORITY=%05o",
                 State->Erasable[0][0176] & 077777,
                 State->Erasable[0][0177] & 077777,
                 State->Erasable[0][0200] & 077777,
                 State->Erasable[0][0201] & 077777,
                 State->Erasable[0][0203] & 077777);
    }
    if (!following) return;

    // EXIT: ISR completed
    if (!isr) {
        ESP_LOGI(KEYRUPT_TAG,
                 "==RESUME== back to Z=%05o DSPLOCK=%05o slot0.LOC=%05o slot0.PRIO=%05o slot1.LOC=%05o slot1.PRIO=%05o",
                 z, State->Erasable[2][012] & 077777,
                 State->Erasable[0][0163] & 077777,
                 State->Erasable[0][0167] & 077777,
                 State->Erasable[0][0177] & 077777,
                 State->Erasable[0][0203] & 077777);
        following = false;
        return;
    }

    // Watched-cell delta notes (fire BEFORE the per-instruction line)
    int dsplock = State->Erasable[2][012];
    int prio0   = State->Erasable[0][0167];
    int loc0    = State->Erasable[0][0163];
    if (dsplock != last_dsplock) {
        ESP_LOGI(KEYRUPT_TAG, "  >> DSPLOCK %05o -> %05o", last_dsplock & 077777, dsplock & 077777);
        last_dsplock = dsplock;
    }
    if (prio0 != last_prio0) {
        ESP_LOGI(KEYRUPT_TAG, "  >> slot0.PRIORITY %05o -> %05o", last_prio0 & 077777, prio0 & 077777);
        last_prio0 = prio0;
    }
    if (loc0 != last_loc0) {
        ESP_LOGI(KEYRUPT_TAG, "  >> slot0.LOC %05o -> %05o", last_loc0 & 077777, loc0 & 077777);
        last_loc0 = loc0;
    }

    // Per-instruction dedup
    if (z == last_z) return;
    last_z = z;

    int a    = State->Erasable[0][RegA] & 077777;
    int l    = State->Erasable[0][RegL] & 077777;
    int q    = State->Erasable[0][RegQ] & 077777;
    int fb   = State->InputChannel[004] & 077777;
    int bb   = State->InputChannel[006] & 077777;
    int eb   = State->InputChannel[003] & 07;
    int ec   = State->ExtraCode;

    ESP_LOGI(KEYRUPT_TAG,
             "Z=%05o A=%05o L=%05o Q=%05o FB=%05o BB=%05o EB=%o ec=%d",
             z, a, l, q, fb, bb, eb, ec);
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
    dispatch_trace_step(State);
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
