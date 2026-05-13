// yaagc_socket.h
//
// Canonical SocketAPI port for ESP-IDF / lwIP. Mirrors the per-cycle
// ChannelInput / ChannelOutput / ChannelRoutine semantics of
// third_party/virtualagc/yaAGC/SocketAPI.c so that peripheral clients
// (LM_Simulator, yaDSKY2, our own peripheral_stub when re-pointed)
// can drive the engine through the same 4-byte protocol they use on
// canonical Pi/Linux yaAGC.
//
// Why this exists: every in-process port of yaAGC's main loop we've
// tried (test_canonical_match, test_simexecute, our channel_router
// integration) fails V37E00E×2, while yaAGC.exe + Python socket driver
// succeeds 5/5. Bisection (2026-05-12) ruled out our integration
// features, the SimExecute loop pattern, and per-channel value
// differences in LM_INI. The remaining structural difference is that
// canonical drives WriteIO + IR5 from *inside* ChannelInput on every
// engine cycle. This component restores that path.
//
// Start the listener once at boot. The accept loop and packet drain
// run from ChannelInput / ChannelRoutine (called by the engine each
// cycle), so no dedicated FreeRTOS task is required.

#ifndef YAAGC_SOCKET_H
#define YAAGC_SOCKET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialise the listener. Returns 0 on success, non-zero on error.
// `port` is the TCP port to bind on 0.0.0.0. Match this to whatever
// the test driver / peripheral client connects to (canonical default
// is 19697; windows_yaagc_test.py picks per-pid ports above 19850).
int yaagc_socket_init(uint16_t port);

// Stop the listener and close all client sockets. Intended for tear-
// down in tests; production firmware just runs forever.
void yaagc_socket_shutdown(void);

// Inject a canonical channel-I/O packet from a LOCAL source (touch,
// web DSKY, peripheral_stub LM_INI). Behaves exactly like a packet
// arriving from a TCP peer — the synthetic-client byte stream picks
// it up on the next ChannelInput drain and routes through the same
// mask-and-WriteIO path that yaAGC.exe + Python driver use.
//
// `is_mask` non-zero means the packet sets ChannelMasks[channel] for
// the synthetic client (matching the upstream pattern of one mask
// packet followed by one value packet per LM_INI channel).
//
// Returns 0 on success, -1 if the synthetic-client ring is full
// (which should never happen in practice — keypresses and LM_INI
// are slow relative to the engine's drain rate).
int yaagc_socket_inject_packet(int channel, int value, int is_mask);

// Convenience: same as inject_packet(015, code, 0). Matches the
// existing channel_router_post_key call sites.
int yaagc_socket_inject_key(int code);

// Inject a DSKY keystroke via the UPRUPT (uplink) path, as if Mission
// Control was driving the AGC. The 5-bit DSKY key code is wrapped in
// the canonical Apollo CCC (Code/Complement/Code) format per
// KEYRUPT,_UPRUPT.agc:74-89 (UPRPT1 routine):
//
//     word = (code << 10) | ((~code & 037) << 5) | code
//
// then sent on channel 0o173 (INLINK), which fires UPRUPT (interrupt 7).
// Luminary's UPRPT1 decodes the triple-redundancy check, validates the
// LOW5+MID5 and LOW5+HI5 sums against HI10=0o77740, and on success
// dispatches to ACCEPTUP -> CHARIN with the 5-bit code in RUPTREG4 —
// identical handling to a real DSKY keystroke.
//
// Building block for future state-vector / pad-load injection
// (V21NXXE typing sequences uplinked the way MCC did during the
// actual mission). Returns 0 on success, -1 on ring full.
int yaagc_socket_inject_uplink_key(int code);

#ifdef __cplusplus
}
#endif

#endif
