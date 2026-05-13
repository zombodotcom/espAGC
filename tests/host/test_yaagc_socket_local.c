// test_yaagc_socket_local — exercises yaagc_socket_inject_packet /
// yaagc_socket_inject_key. NO TCP CLIENT. The whole LM_INI + key
// sequence comes from local injection, which is how channel_router /
// peripheral_stub will feed the canonical layer on ESP32 hardware.
//
// If this passes 5/5 (or 1/1 in this build), the local-source path
// works identically to the TCP-driven path that already passed 5/5 in
// test_yaagc_socket_host.exe. Then we know the wiring on ESP32 will
// work whether keys come from touch, web DSKY, peripheral_stub, or a
// TCP peer — they all feed the same canonical mask+value drain inside
// ChannelInput.
//
// Build: mingw32-make test_yaagc_socket_local.exe
// Run:   ROM=../../build/roms/Luminary099.bin ./test_yaagc_socket_local.exe

#include "yaAGC.h"
#include "agc_engine.h"
#include "yaagc_socket.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#ifdef _WIN32
#include <windows.h>
static void sleep_ms(int ms) { Sleep(ms); }
static void sim_sleep_10ms(void) { Sleep(10); }
#else
#include <time.h>
#include <unistd.h>
static void sleep_ms(int ms) { usleep(ms * 1000); }
static void sim_sleep_10ms(void) {
    struct timespec t = {0, 10000000};
    nanosleep(&t, NULL);
}
#endif

#define CLK_TCK 100
static long sim_times(void)
{
#ifdef _WIN32
    static LARGE_INTEGER freq = {0};
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return (long)((t.QuadPart * CLK_TCK) / freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)(ts.tv_sec * CLK_TCK + (ts.tv_nsec * CLK_TCK) / 1000000000L);
#endif
}

extern int agc_engine_init(agc_t *State, const char *RomImage,
                           const char *CoreDump, int AllOrErasable);
extern void yaagc_socket_channel_output(agc_t *State, int Channel, int Value);
extern int  yaagc_socket_channel_input (agc_t *State);
extern void yaagc_socket_channel_routine(agc_t *State);

void ChannelOutput(agc_t *State, int Channel, int Value)
{ yaagc_socket_channel_output(State, Channel, Value); }
int  ChannelInput (agc_t *State) { return yaagc_socket_channel_input(State); }
void ChannelRoutine(agc_t *State) { yaagc_socket_channel_routine(State); }
void ShiftToDeda(agc_t *State, int Data) { (void)State; (void)Data; }
void RequestRadarData(agc_t *State) { (void)State; }

static agc_t state;
agc_t *agc_core_state(void) { return &state; }

static volatile int g_hit_55265 = 0;

// Driver thread: pace LM_INI then keys at canonical timings.
static int ascii_to_key(char c)
{
    switch (c) {
        case 'V': return 17; case 'N': return 31;
        case '+': return 26; case '-': return 27;
        case 'R': return 18; case 'E': return 28;
        case 'C': return 30; case 'P': return 25;
        case '0': return 16;
        case '1': return 1; case '2': return 2; case '3': return 3;
        case '4': return 4; case '5': return 5; case '6': return 6;
        case '7': return 7; case '8': return 8; case '9': return 9;
        default: return -1;
    }
}

static const struct { int ch; int val; int mask; } LM_INI[] = {
    {0o16, 0,        0o00174},
    {0o30, 0o36331,  0o77777},
    {0o31, 0o77777,  0o77777},
    {0o32, 0o22777,  0o77777},
    {0o33, 0o57776,  0o77776},
};

static void *driver(void *arg)
{
    const char *seq = (const char *)arg;
    sleep_ms(500);
    // Send canonical LM_INI: mask packet, then value packet, per channel.
    for (size_t i = 0; i < sizeof(LM_INI)/sizeof(LM_INI[0]); i++) {
        yaagc_socket_inject_packet(LM_INI[i].ch, LM_INI[i].mask, 1);
        yaagc_socket_inject_packet(LM_INI[i].ch, LM_INI[i].val,  0);
    }
    sleep_ms(2000);
    for (const char *p = seq; *p; p++) {
        if (*p == ' ') { sleep_ms(3000); continue; }
        int code = ascii_to_key(*p);
        if (code >= 0) {
            yaagc_socket_inject_key(code);
            sleep_ms(100);
        }
    }
    sleep_ms(2000);
    return NULL;
}

int main(int argc, char **argv)
{
    const char *rom = getenv("ROM");
    if (!rom) rom = "../../build/roms/Luminary099.bin";
    const char *seq = getenv("SEQ");
    if (!seq) seq = "R V36E V37E 00E V37E 00E";

    int rc = agc_engine_init(&state, rom, NULL, 0);
    if (rc != 0) { fprintf(stderr, "agc_engine_init: %d\n", rc); return 1; }

    // Listener still comes up, but nothing connects. The whole show is
    // driven via inject_packet from the driver thread.
    if (yaagc_socket_init(0) != 0) {
        // Port 0 → kernel picks any free port; we don't care which because
        // we're not actually accepting connections in this test.
    }
    fprintf(stderr, "test_yaagc_socket_local: seq='%s' (local-injection only)\n", seq);

    pthread_t tid;
    pthread_create(&tid, NULL, driver, (void *)seq);

    long real_time_offset = sim_times();
    long cycle_count = CLK_TCK * (long)state.CycleCounter;
    real_time_offset -= (cycle_count + AGC_PER_SECOND / 2) / AGC_PER_SECOND;
    long last_real_time = ~(long)0;
    long desired_cycles = 0;
    long deadline = sim_times() + 30 * CLK_TCK;
    int  last_oc11 = -1;
    long hit_cycle = 0;

    while (sim_times() < deadline) {
        long now_t = sim_times();
        if (now_t != last_real_time) {
            last_real_time = now_t;
            desired_cycles = (now_t - real_time_offset) * AGC_PER_SECOND;
        } else {
            sim_sleep_10ms();
            continue;
        }
        while (cycle_count < desired_cycles) {
            agc_engine(&state);
            cycle_count += CLK_TCK;
            int oc11 = state.OutputChannel10[11] & 077777;
            if (oc11 != last_oc11) {
                last_oc11 = oc11;
                if (oc11 == 055265 && !g_hit_55265) {
                    g_hit_55265 = 1;
                    hit_cycle = cycle_count / CLK_TCK;
                    fprintf(stderr, "*** PRG=00 emitted at cycle %ld ***\n", hit_cycle);
                }
            }
        }
    }
    pthread_join(tid, NULL);

    fprintf(stderr, "FINAL: cyc=%llu OC10[11]=%05o Z=%05o\n",
            (unsigned long long)state.CycleCounter,
            state.OutputChannel10[11] & 077777,
            state.Erasable[0][5] & 077777);
    if (g_hit_55265) {
        printf("RESULT: PRG=00 SUCCESS at cycle %ld\n", hit_cycle);
        return 0;
    }
    printf("RESULT: PRG=00 NOT REACHED\n");
    return 1;
}
