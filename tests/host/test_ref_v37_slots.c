// test_ref_v37_slots — drives the V37E00E sequence against UPSTREAM
// yaAGC engine init (no peripheral_stub, no rescues), and dumps slot 0
// state at each step. Tells us whether the second-V37+ENTR crash is
// (a) inherent to Luminary without LM_Simulator feeding peripherals, or
// (b) caused by our peripheral_stub/rescue logic.
//
// Build:  mingw32-make test_ref_v37_slots.exe
// Run:    ROM=../../build/roms/Luminary099.bin ./test_ref_v37_slots.exe

#include "yaAGC.h"
#include "agc_engine.h"
#include "peripheral_stub.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int agc_engine_init(agc_t *State, const char *RomImage,
                           const char *CoreDump, int AllOrErasable);

static agc_t state;
agc_t *agc_core_state(void) { return &state; }

static int g_use_sim = 0;
static long g_cycles_since_sim = 0;
static void step(int n) {
    for (int i = 0; i < n; i++) {
        agc_engine(&state);
        if (g_use_sim) {
            g_cycles_since_sim++;
            if (g_cycles_since_sim >= 10000) {  // ~10ms at 1 MHz nominal
                peripheral_stub_step(&state, 10000);
                g_cycles_since_sim = 0;
            }
        }
    }
}

static void post_key(int code) {
    state.InputChannel[015] = code & 037;
    state.InterruptRequests[5] = 1;
}

static void dump_slots(const char *label) {
    int z = state.Erasable[0][5] & 077777;
    int modreg = state.Erasable[2][0011] & 077777;
    int verbreg = state.Erasable[2][0001] & 077777;
    int nounreg = state.Erasable[2][0002] & 077777;
    printf("[%-14s] cyc=%9lld Z=%05o MODREG=%06o VERB=%06o NOUN=%06o\n",
           label, (long long)state.CycleCounter, z, modreg, verbreg, nounreg);
    printf("  slot 0: ");
    for (int i = 0; i < 12; i++) printf("%05o ", state.Erasable[0][0154+i] & 077777);
    printf("\n");
    for (int s = 1; s < 8; s++) {
        int b = 0154 + s * 014;
        int p = state.Erasable[0][b + 11] & 077777;
        int l = state.Erasable[0][b + 8] & 077777;
        if (p != 077777 && p != 0)
            printf("  slot %d: prio=%06o loc=%06o\n", s, p, l);
    }
}

static void key(int code, int n) {
    post_key(code);
    step(n);
}

int main(int argc, char **argv) {
    const char *rom = getenv("ROM");
    if (!rom) rom = "../../build/roms/Luminary099.bin";
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--sim")) g_use_sim = 1;

    int rc = agc_engine_init(&state, rom, NULL, 0);
    if (rc != 0) { fprintf(stderr, "agc_engine_init failed: %d\n", rc); return 1; }
    if (g_use_sim) peripheral_stub_init();
    fprintf(stderr, "use_sim=%d\n", g_use_sim);

    step(2500000);                            dump_slots("ini");
    key(15, 500000);                          dump_slots("R");
    key(17,100000);key(3,100000);key(7,100000);key(28,3000000); dump_slots("V37E #1");
    key(16,100000);key(16,100000);key(28,5000000); dump_slots("00E #1");
    key(17,100000);key(3,100000);key(7,100000);key(28,3000000); dump_slots("V37E #2");
    key(16,100000); dump_slots("0 #1");
    key(16,100000); dump_slots("0 #2");
    post_key(28);
    for (int i = 0; i < 5; i++) {
        step(100000);
        char l[16]; sprintf(l, "E +%dK", (i+1)*100); dump_slots(l);
    }
    return 0;
}
