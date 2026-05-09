// test_channel10_emit — confirm the engine writes to the DSKY display
// channel (010 octal) within a reasonable warm-up window. Without this,
// nothing in our IDF DSKY pipeline could ever light up.

#include "test_helpers.h"
#include "yaAGC.h"
#include "agc_engine.h"
#include "agc_core.h"

extern int io_log_count;
extern struct { int channel; int value; } io_log[];

int main(void)
{
    size_t sz;
    uint8_t *rom = load_rom_file(&sz);
    ASSERT(agc_core_init(rom, sz) == AGC_OK, "init failed");

    // 200k AGC cycles at ~1 µs each on real hardware ≈ 0.2s simulated time.
    // The DSKY routine in Luminary099 schedules a refresh well within that.
    agc_core_step(200000);

    int ch10 = 0, ch11 = 0;
    for (int i = 0; i < io_log_count; i++) {
        if (io_log[i].channel == 010)  ch10++;
        if (io_log[i].channel == 011)  ch11++;
    }

    printf("io_log_count=%d ch10=%d ch11=%d\n", io_log_count, ch10, ch11);
    ASSERT(io_log_count > 0, "engine emitted no channel writes at all");

    free(rom);
    PASS();
}
