// test_rom_load — verify our memory ROM loader populates the engine's bank
// array with the expected layout. Reads a few words at known addresses in
// Luminary099 and checks against the documented values.

#include "test_helpers.h"
#include "yaAGC.h"
#include "agc_engine.h"
#include "agc_core.h"

int main(void)
{
    size_t sz;
    uint8_t *rom = load_rom_file(&sz);
    ASSERT(sz == 36 * 02000 * 2, "ROM size %zu, expected %d", sz, 36 * 02000 * 2);

    int rc = agc_core_init(rom, sz);
    ASSERT(rc == AGC_OK, "agc_core_init returned %d", rc);

    // Banks in the file are stored in order 2, 3, 0, 1, 4, 5, ..., 35.
    // One bank = 02000 words = 2048 bytes = 0x800. Spot-check the mapping.
    extern agc_t *agc_core_state(void);
    agc_t *st = agc_core_state();

    static const struct { int agc_bank; size_t file_off; } slots[] = {
        { 2, 0x0000 },   // file slot 0 → AGC bank 2
        { 3, 0x0800 },   // file slot 1 → AGC bank 3
        { 0, 0x1000 },   // file slot 2 → AGC bank 0
        { 1, 0x1800 },   // file slot 3 → AGC bank 1
        { 4, 0x2000 },   // file slot 4 → AGC bank 4
    };
    for (size_t i = 0; i < sizeof(slots)/sizeof(slots[0]); i++) {
        uint16_t raw = ((uint16_t)rom[slots[i].file_off] << 8)
                     |  (uint16_t)rom[slots[i].file_off + 1];
        int16_t expected = (int16_t)(raw >> 1);
        ASSERT(st->Fixed[slots[i].agc_bank][0] == expected,
               "Fixed[%d][0]=0%o expected 0%o (file offset 0x%zx)",
               slots[i].agc_bank, st->Fixed[slots[i].agc_bank][0],
               expected, slots[i].file_off);
    }

    // Initial program counter must be 04000 (boot vector).
    ASSERT(st->Erasable[0][RegZ] == 04000,
           "RegZ=0%o expected 04000", st->Erasable[0][RegZ]);

    free(rom);
    PASS();
}
