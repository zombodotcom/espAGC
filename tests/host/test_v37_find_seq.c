// Try various keystroke sequences via real-time pacing, see which (if
// any) reaches PRG=00 (ch010=55265). Drops each result.
//
// Iterates through candidate sequences in argv; runs each from a
// clean boot and reports whether ch010=55265 emitted.

#include "agc_harness.h"
#include "test_helpers.h"
#include "yaAGC.h"
#include "agc_engine.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
static void sleep_us(long us) { if (us >= 1000) Sleep((DWORD)(us / 1000)); }
#else
static void sleep_us(long us) {
    struct timespec ts = {us / 1000000, (us % 1000000) * 1000};
    nanosleep(&ts, NULL);
}
#endif

extern agc_t *agc_core_state(void);

static int key_for(char c) {
    switch (c) {
        case 'V': return 17;
        case 'N': return 31;
        case '+': return 26;
        case '-': return 27;
        case 'R': return 15;  // RSET in DSKY_KEY namespace
        case 'E': return 28;
        case 'C': return 30;
        case 'P': return 25;
        case '0': return 16;
        case '1': return 1;  case '2': return 2;  case '3': return 3;
        case '4': return 4;  case '5': return 5;  case '6': return 6;
        case '7': return 7;  case '8': return 8;  case '9': return 9;
        default: return -1;
    }
}

#define MS 1000
struct keyplan { int code; int delay_us; };

static void *key_thread(void *arg) {
    struct keyplan *plan = (struct keyplan *)arg;
    for (int i = 0; plan[i].delay_us >= 0; i++) {
        if (plan[i].delay_us > 0) sleep_us(plan[i].delay_us);
        if (plan[i].code != 0) harness_post_key(plan[i].code);
    }
    return NULL;
}

// Watch for ch010 = 55265 (PRG=00). Inspect snapshot.prog[0]+[1]==0.
volatile int g_saw_prg00 = 0;
volatile int g_saw_modreg_zero = 0;

static void check_state(void) {
    agc_t *s = agc_core_state();
    // MODREG @ 01011 = bank2 off 011: 0 means P00 selected
    int modreg = s->Erasable[2][0011] & 077777;
    if (modreg == 0) g_saw_modreg_zero = 1;
    // Check snapshot for both PROG digits being 0
    dsky_state_t ss;
    harness_snapshot(&ss);
    if (ss.prog[0] == 0 && ss.prog[1] == 0) g_saw_prg00 = 1;
}

static int run_seq(const char *seq_str) {
    harness_boot();
    g_saw_prg00 = 0;
    // Build keyplan from sequence string. Tokens separated by space, keys
    // within a token separated by KEY_GAP_US, tokens by TOKEN_GAP_US.
    struct keyplan plan[256];
    int n = 0;
    plan[n].code = 0; plan[n].delay_us = 2500 * MS; n++;
    int first_in_token = 1;
    for (const char *p = seq_str; *p; p++) {
        if (*p == ' ' || *p == '\t') {
            if (!first_in_token) { plan[n].code = 0; plan[n].delay_us = 3000 * MS; n++; first_in_token = 1; }
            continue;
        }
        int k = key_for(*p);
        if (k < 0) continue;
        plan[n].code = k;
        plan[n].delay_us = first_in_token ? 0 : 100 * MS;
        first_in_token = 0;
        n++;
    }
    plan[n].code = 0; plan[n].delay_us = 3000 * MS; n++;
    plan[n].code = 0; plan[n].delay_us = -1; n++;

    pthread_t tid;
    pthread_create(&tid, NULL, key_thread, plan);

    // 20 sec wall clock with 100k-cycle batches, paced at 100ms each
    int total = 200;
    for (int i = 0; i < total && !g_saw_prg00; i++) {
        harness_step(100000);
        check_state();
        sleep_us(100 * 1000);
    }
    pthread_join(tid, NULL);
    fprintf(stderr, "  MODREG=0 seen: %d\n", g_saw_modreg_zero);
    return g_saw_prg00;
}

int main(int argc, char **argv) {
    const char *seqs[] = {
        "R V36E V37E 00E V37E 00E",
        "R V35E V37E 00E V37E 00E",
        "R V37E 00E V37E 00E",
        "R V37E 00E",
        "R V37E 00E R V37E 00E",
        "R V36E V37E 00E",
        NULL
    };
    for (int i = 0; seqs[i]; i++) {
        fprintf(stderr, "=== sequence: %s\n", seqs[i]);
        int got = run_seq(seqs[i]);
        fprintf(stderr, "  PRG=00 %s\n", got ? "EMITTED" : "missing");
        fflush(stderr);
    }
    return 0;
}
