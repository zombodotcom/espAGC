// test_redoctr — sample REDOCTR which counts software RESTARTs
// (via GOPROG's `INCR REDOCTR`). Many restarts = software POODOOs
// firing.

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

    int last_rc = -1;
    for (long c = 0; c < 50000000; c++) {
        agc_engine(st);
        int rc = st->Erasable[0][0320] & 077777;
        if (rc != last_rc) {
            int z = st->Erasable[0][5] & 07777;
            printf("c=%9ld REDOCTR=%05o Z=%05o\n", c, rc, z);
            last_rc = rc;
        }
    }
    PASS();
}
