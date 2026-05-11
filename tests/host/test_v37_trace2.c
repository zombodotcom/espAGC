// test_v37_trace2 — trace all key addresses in the V37E00E dispatch chain
// to find where the chain breaks.

#include "agc_harness.h"
#include "test_helpers.h"
#include "yaAGC.h"
#include "agc_engine.h"
#include <stdio.h>

extern agc_t *agc_core_state(void);
extern int agc_engine(agc_t *State);

static int cur_z(agc_t *s)  { return s->Erasable[0][RegZ] & 07777; }
static int cur_fb(agc_t *s) { return (s->Erasable[0][RegBB] & 076000) >> 10; }
static int cur_superbnk(agc_t *s) { return (s->OutputChannel7 & 0100) ? 1 : 0; }

static int eff_bank(agc_t *s)
{
    int fb = cur_fb(s);
    if (fb >= 030 && cur_superbnk(s)) return fb + 010;
    return fb;
}

typedef struct {
    int bank;       // octal effective bank
    int z;          // octal address
    const char *name;
    long hits;
    long long first;
} probe_t;

static probe_t probes[] = {
    {040, 02077, "CHARIN",   0, -1},   // CHARIN entry
    {041, 02133, "VERBFAN",  0, -1},
    {041, 03430, "MMCHANG",  0, -1},
    {041, 03431, "MMCHANG+1",0, -1},
    {041, 02012, "ENTPASHI", 0, -1},
    {041, 03450, "POSTJMP_MODROUTB", 0, -1},
    {041, 03435, "ALMCYCLE_branch", 0, -1},  // TC ALMCYCLE if DSPCOUNT != -ND2
    {04, 02040, "V37",       0, -1},
    {04, 02065, "CANTROO",   0, -1},
    {04, 02163, "POOH",      0, -1},
    {04, 02134, "CANV37",    0, -1},
    {04, 02220, "GOMOD",     0, -1},
    {04, 02223, "TS MODREG", 0, -1},
    {04, 02166, "CLRADMOD_call", 0, -1},
    {04, 02204, "ENGINOF1_call", 0, -1},
    {04, 02206, "ALLCOAST_call", 0, -1},
    {04, 02214, "V37KLEAN_call", 0, -1},
    {04, 02216, "CCS_MMNUMBER", 0, -1},
    {05, 02647, "POOKLEAN", 0, -1},
    {05, 02652, "V37KLEAN", 0, -1},
    {020, 02204, "ALLCOAST", 0, -1},
    {036, 03555, "ENGINOF1", 0, -1},
    {013, 03414, "INTSTALL", 0, -1},
    {013, 03460, "OKTOGRAB", 0, -1},
    {013, 03425, "INTWAKE", 0, -1},
    {013, 03445, "INTWAKE1", 0, -1},
    {04, 02144, "ROO+1(CALL)", 0, -1},
    {04, 02146, "DUMMYAD",  0, -1},
    {04, 02147, "TC DOWNFLAG", 0, -1},
};
static const int NPROBES = sizeof(probes)/sizeof(probes[0]);

static long long g_cyc = 0;

static void tick(agc_t *s) {
    int z = cur_z(s);
    int b = eff_bank(s);
    for (int i = 0; i < NPROBES; i++) {
        if (probes[i].bank == b && probes[i].z == z) {
            probes[i].hits++;
            if (probes[i].first < 0) probes[i].first = g_cyc;
        }
    }
    agc_engine(s);
    g_cyc++;
}

static void run(agc_t *s, long n) { for (long i = 0; i < n; i++) tick(s); }

static void report(const char *tag) {
    printf("=== %s (cyc=%lld) ===\n", tag, g_cyc);
    for (int i = 0; i < NPROBES; i++) {
        printf("  %s (%02o,%05o) hits=%-6ld first=%lld\n",
               probes[i].name, probes[i].bank, probes[i].z,
               probes[i].hits, probes[i].first);
    }
}

int main(void)
{
    harness_boot();
    agc_t *s = agc_core_state();

    run(s, 200000);
    harness_post_key(15);   // RSET
    run(s, 50000);
    report("boot+R");

    harness_post_key(17);   // V
    run(s, 50000);
    report("after V");

    harness_post_key(3);
    run(s, 50000);
    report("after 3");

    harness_post_key(7);
    run(s, 50000);
    report("after 7");

    harness_post_key(28);   // E
    run(s, 50000);
    report("after V37E (first E)");

    harness_post_key(16);   // 0
    run(s, 50000);
    report("after 0");

    harness_post_key(16);
    run(s, 50000);
    report("after 0 0");

    harness_post_key(28);   // E
    run(s, 50000);
    report("after V37E00E (second E)");

    run(s, 1000000);
    report("after V37E00E +1M cycles");

    // Final state dump
    int modreg = s->Erasable[2][011] & 077777;
    int mmnumber = s->Erasable[1][0375] & 077777;
    int failreg0 = s->Erasable[0][0375] & 077777;
    int failreg1 = s->Erasable[0][0376] & 077777;
    int failreg2 = s->Erasable[0][0377] & 077777;
    int restartlight = s->RestartLight;
    int rasflag = s->Erasable[0][0106] & 077777;
    printf("\n=== final state ===\n");
    printf("  MODREG=%05o MMNUMBER=%05o\n", modreg, mmnumber);
    printf("  FAILREG=[%05o,%05o,%05o] RestartLight=%d\n",
           failreg0, failreg1, failreg2, restartlight);
    printf("  RASFLAG=%05o\n", rasflag);
    printf("  current Z=%05o FB=%02o eff_bank=%02o\n",
           cur_z(s), cur_fb(s), eff_bank(s));

    PASS();
}
