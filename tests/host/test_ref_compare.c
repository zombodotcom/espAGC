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

int main(void)
{
    harness_boot();
    // 0.5s cold-boot settle
    harness_step(500000);

    section("--- sending LM_Sim ini values ---");
    // (peripheral_stub_init already wrote the channel baselines)

    harness_step(2000000);
    section("--- cold-boot settle (2s) ---");

    section("--- RSET ---");
    harness_post_key(15); harness_step(500000);

    section("--- V35E (lamp test) ---");
    harness_post_key(17); harness_step(100000);  // V
    harness_post_key(3);  harness_step(100000);
    harness_post_key(5);  harness_step(100000);
    harness_post_key(28); harness_step(3000000); // E + settle

    section("--- V37E ---");
    harness_post_key(17); harness_step(100000);
    harness_post_key(3);  harness_step(100000);
    harness_post_key(7);  harness_step(100000);
    harness_post_key(28); harness_step(1000000);

    section("--- 00E ---");
    harness_post_key(16); harness_step(100000);
    harness_post_key(16); harness_step(100000);
    harness_post_key(28); harness_step(5000000);

    section("--- DONE ---");
    return 0;
}
