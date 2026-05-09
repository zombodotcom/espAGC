#pragma once
//
// Public API of the AGC emulator wrapper. Wraps the upstream yaAGC engine
// (third_party/virtualagc/yaAGC) and replaces its socket-based DSKY transport
// with in-process callbacks routed through the channel_router component.
//
// Threading model: agc_core_step() runs in a single dedicated FreeRTOS task.
// All other entry points must only be called when that task is suspended
// or before it has been started.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Init result codes mirror yaAGC conventions where applicable.
typedef enum {
    AGC_OK              = 0,
    AGC_ERR_ROM_TOO_BIG = 2,
    AGC_ERR_ROM_ODD     = 3,
    AGC_ERR_NO_STATE    = 4,
} agc_status_t;

// Initialize the AGC engine with a ROM image already resident in memory
// (e.g. an EMBED_FILES blob from the apollo_rom component). The buffer must
// remain valid for the lifetime of the engine.
agc_status_t agc_core_init(const uint8_t *rom, size_t rom_size);

// Run a batch of AGC instructions on the calling task. Returns the number of
// AGC cycles consumed. A typical UI cadence calls this with batch_size on the
// order of a few thousand cycles.
int agc_core_step(int batch_size);

// Forwarded to the engine: dirty all DSKY-related output channels so the next
// emulator pass re-emits ChannelOutput callbacks for ch10 (and ch11).
// Useful after the UI reconnects, since the engine only re-emits on change.
void agc_core_force_dsky_refresh(void);

#ifdef __cplusplus
}
#endif
