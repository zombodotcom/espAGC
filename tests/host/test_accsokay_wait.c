// test_accsokay_wait — run for many seconds; watch when (if ever)
// ACCSOKAY bit gets set in DAPBOOLS. If it never sets, 1/ACCS isn't
// completing, and DAPIDLER stays in MOREIDLE blocking everything.

#include "agc_harness.h"
#include "test_helpers.h"
#include "yaAGC.h"
#include "agc_engine.h"
#include "dsky_keys.h"
#include <stdio.h>

extern agc_t *agc_core_state(void);
extern int agc_engine(agc_t *st);

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    int last_dap = -1;
    int max_dap = 0;
    int accsokay_seen = 0;
    long first_accsokay = -1;

    for (long c = 0; c < 50000000; c++) {  // 50M cycles
        agc_engine(st);
        int dap = st->Erasable[0][0111] & 077777;
        if (dap != last_dap) {
            printf("c=%9ld DAPBOOLS=%05o\n", c, dap);
            last_dap = dap;
            if (dap > max_dap) max_dap = dap;
            if ((dap & 4) && !accsokay_seen) {
                accsokay_seen = 1;
                first_accsokay = c;
                printf("*** ACCSOKAY SET at c=%ld ***\n", c);
            }
        }
    }
    printf("\nMax DAPBOOLS seen: %05o\n", max_dap);
    printf("ACCSOKAY set: %s (first c=%ld)\n",
           accsokay_seen ? "YES" : "NO", first_accsokay);

    int mass = st->Erasable[2][0244] & 077777;
    printf("MASS final: %05o\n", mass);
    PASS();
}
