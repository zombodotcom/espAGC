#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APOLLO_ROM_LUMINARY099 = 0,   // Apollo 11 LM
    APOLLO_ROM_COMANCHE055 = 1,   // Apollo 11 CSM
    APOLLO_ROM_COUNT
} apollo_rom_id_t;

// Returns a pointer to the embedded mission ROM and writes its byte length to
// *out_size. The pointer is to read-only flash and remains valid for the
// lifetime of the program.
const uint8_t *apollo_rom_get(apollo_rom_id_t id, size_t *out_size);

const char    *apollo_rom_name(apollo_rom_id_t id);

#ifdef __cplusplus
}
#endif
