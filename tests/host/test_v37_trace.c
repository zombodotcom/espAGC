// test_v37_trace — step one cycle at a time after V37E00E and watch for
// entry into V37 (FB=04, Z=02040). If V37 is never entered, the dispatch
// path before V37 is broken. If V37 is entered, log the next several
// addresses to see where the chain diverges before MODREG would be set
// at FB=04, Z=02223.

#include "agc_harness.h"
#include "test_helpers.h"
#include "yaAGC.h"
#include "agc_engine.h"
#include <stdio.h>

extern agc_t *agc_core_state(void);
extern int agc_engine(agc_t *State);

// V37: bank 04, addr 02040.
#define V37_FB    010000   // (04 << 10) octal = 4 << 10 = 010000
#define V37_Z     02040

// MODREG assignment: bank 04, addr 02223 (TS MODREG line 9462).
#define MODREG_FB 010000
#define MODREG_Z  02223

// CANTROO alarm: bank 04, addr 02065.
#define CANTROO_Z 02065

// V37BAD: bank 04, addr 02067.
#define V37BAD_Z  02067

// V37NONO: bank 04, addr 02322.
#define V37NONO_Z 02322

// POOH: bank 04, addr 02163.
#define POOH_Z    02163

// GOMOD: bank 04, addr 02220.
#define GOMOD_Z   02220

static int cur_fb(agc_t *s) { return s->Erasable[0][RegBB] & 076000; }
static int cur_z(agc_t *s)  { return s->Erasable[0][RegZ] & 07777; }

int main(void)
{
    harness_boot();
    harness_step(200000);
    harness_type("R", 50000);
    harness_type("V37E00E", 200000);

    agc_t *s = agc_core_state();

    long v37_entries = 0;
    long modreg_writes = 0;
    long cantroo_hits = 0;
    long v37bad_hits = 0;
    long v37nono_hits = 0;
    long pooh_hits = 0;
    long gomod_hits = 0;
    long long first_v37_cyc = -1;
    long long first_modreg_cyc = -1;
    long long total_cycles = 0;

    // Step 50M cycles one at a time, watching addresses.
    // Once V37 is hit, capture the next 200 unique Z values.
    int captured = 0;
    int last_z = -1, last_fb = -1;
    int trace_after_v37 = 0;

    for (long long i = 0; i < 50000000LL; i++) {
        int z = cur_z(s);
        int fb = cur_fb(s);

        if (fb == V37_FB && z == V37_Z) {
            v37_entries++;
            if (first_v37_cyc < 0) first_v37_cyc = total_cycles;
            trace_after_v37 = 250;
        }
        if (fb == MODREG_FB && z == MODREG_Z) {
            modreg_writes++;
            if (first_modreg_cyc < 0) first_modreg_cyc = total_cycles;
        }
        if (fb == V37_FB && z == CANTROO_Z) cantroo_hits++;
        if (fb == V37_FB && z == V37BAD_Z)  v37bad_hits++;
        if (fb == V37_FB && z == V37NONO_Z) v37nono_hits++;
        if (fb == V37_FB && z == POOH_Z)    pooh_hits++;
        if (fb == V37_FB && z == GOMOD_Z)   gomod_hits++;

        if (trace_after_v37 > 0 && (z != last_z || fb != last_fb)) {
            trace_after_v37--;
            if (captured < 100) {
                printf("  trace[%d] FB=%05o Z=%05o cyc=%lld\n",
                       captured, fb, z, total_cycles);
                captured++;
            }
            last_z = z;
            last_fb = fb;
        }

        agc_engine(s);
        total_cycles++;
    }

    printf("\n=== summary ===\n");
    printf("  V37 (04,2040)     entries=%ld first_cyc=%lld\n", v37_entries, first_v37_cyc);
    printf("  CANTROO (04,2065) hits=%ld\n", cantroo_hits);
    printf("  V37BAD (04,2067)  hits=%ld\n", v37bad_hits);
    printf("  V37NONO (04,2322) hits=%ld\n", v37nono_hits);
    printf("  POOH (04,2163)    hits=%ld\n", pooh_hits);
    printf("  GOMOD (04,2220)   hits=%ld\n", gomod_hits);
    printf("  MODREG=A (04,2223) writes=%ld first_cyc=%lld\n", modreg_writes, first_modreg_cyc);
    printf("  total_cycles=%lld\n", total_cycles);

    int modreg_val = s->Erasable[2][011] & 077777;
    int mmnumber = s->Erasable[1][0375] & 077777;
    printf("  end MODREG=%05o MMNUMBER=%05o\n", modreg_val, mmnumber);

    PASS();
}
