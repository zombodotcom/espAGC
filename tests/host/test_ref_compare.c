// test_ref_compare — runs the same keystroke sequence as ref_capture.py
// (RSET, V35E, V37E00E) and emits raw output channel writes in the same
// format. Run with:
//
//   make test_ref_compare.exe
//   ROM=../../build/roms/Luminary099.bin ./test_ref_compare.exe > local.log
//   diff -u golden/ref_V35E_V37E00E.log local.log | head
//
// Build with -DCONFIG_AGC_LOG_ALL_OUTPUTS=1 (Makefile takes care of this
// for this test).

#include "agc_harness.h"
#include "test_helpers.h"
#include <stdio.h>
#include <unistd.h>

static void section(const char *label) { printf("%s\n", label); fflush(stdout); }

// Matches tests/host/golden/ref_V36_V37E00E_double_to_PRG00.log
// captured from reference yaAGC. The reference produces PRG=00
// (ch010 = 55265) after the SECOND V37E00E. Our build should match.
static void key(int code, int step_cycles) {
    harness_post_key(code); harness_step(step_cycles);
}

int main(void)
{
    harness_boot();
    section("--- ini ---");
    harness_step(500000);
    harness_step(2000000);

    section("--- R ---");
    key(15, 500000);

    section("--- V36E ---");
    key(17, 100000); key(3, 100000); key(6, 100000);
    key(28, 3000000);

    section("--- V37E ---");
    key(17, 100000); key(3, 100000); key(7, 100000);
    key(28, 1000000);

    section("--- 00E ---");
    key(16, 100000); key(16, 100000);
    key(28, 3000000);

    section("--- V37E ---");
    key(17, 100000); key(3, 100000); key(7, 100000);
    key(28, 5000000);

    section("--- 00E ---");
    key(16, 100000); key(16, 100000);
    key(28, 15000000);

    section("--- DONE ---");
    return 0;
}
