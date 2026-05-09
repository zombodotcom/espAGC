#include "apollo_rom.h"

extern const uint8_t Luminary099_bin_start[] asm("_binary_Luminary099_bin_start");
extern const uint8_t Luminary099_bin_end[]   asm("_binary_Luminary099_bin_end");
extern const uint8_t Comanche055_bin_start[] asm("_binary_Comanche055_bin_start");
extern const uint8_t Comanche055_bin_end[]   asm("_binary_Comanche055_bin_end");

const uint8_t *apollo_rom_get(apollo_rom_id_t id, size_t *out_size)
{
    switch (id) {
    case APOLLO_ROM_LUMINARY099:
        if (out_size) *out_size = (size_t)(Luminary099_bin_end - Luminary099_bin_start);
        return Luminary099_bin_start;
    case APOLLO_ROM_COMANCHE055:
        if (out_size) *out_size = (size_t)(Comanche055_bin_end - Comanche055_bin_start);
        return Comanche055_bin_start;
    default:
        if (out_size) *out_size = 0;
        return NULL;
    }
}

const char *apollo_rom_name(apollo_rom_id_t id)
{
    switch (id) {
    case APOLLO_ROM_LUMINARY099: return "Luminary099";
    case APOLLO_ROM_COMANCHE055: return "Comanche055";
    default:                     return "?";
    }
}
