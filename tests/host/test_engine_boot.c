// test_engine_boot — load Luminary099, run 50 000 AGC cycles, assert the
// engine made forward progress without crashing and that CycleCounter
// advanced.

#include "test_helpers.h"
#include "yaAGC.h"
#include "agc_engine.h"
#include "agc_core.h"

int main(void)
{
    size_t sz;
    uint8_t *rom = load_rom_file(&sz);
    ASSERT(agc_core_init(rom, sz) == AGC_OK, "init failed");

    extern agc_t *agc_core_state(void);
    agc_t *st = agc_core_state();
    uint64_t cyc0 = st->CycleCounter;

    int n = agc_core_step(50000);
    ASSERT(n == 50000, "step returned %d, expected 50000", n);
    ASSERT(st->CycleCounter > cyc0, "CycleCounter did not advance");

    // After 50k cycles the engine should be off the boot vector.
    ASSERT(st->Erasable[0][RegZ] != 04000, "still at boot vector");

    free(rom);
    PASS();
}
