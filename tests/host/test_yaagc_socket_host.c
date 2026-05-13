// test_yaagc_socket_host — host build of yaagc_socket layer driving the
// real agc_engine via SocketAPI-style ChannelInput/Output/Routine. Listens
// on TCP 19850 so windows_yaagc_test.py can drive it identically to how it
// drives yaAGC.exe.
//
// Purpose: validate that the canonical SocketAPI semantics we ported into
// components/yaagc_socket/ are sufficient to recover V37E00E×2. If
// windows_yaagc_test.py reports 5/5 against this binary, the architecture
// port is the right fix and we move on to bringing it up on QEMU /
// hardware. If it reports <5/5, the bug is deeper than packet plumbing
// and we need a per-cycle ch005..0177 diff vs yaAGC.exe.
//
// Build: mingw32-make test_yaagc_socket_host.exe
// Run:   ROM=../../build/roms/Luminary099.bin ./test_yaagc_socket_host.exe
//        (in another terminal:)  py windows_yaagc_test.py 5
//        — but point its YAAGC binary at this exe, OR temporarily edit
//        the script to skip subprocess launch and just connect to 19850.

#include "yaAGC.h"
#include "agc_engine.h"
#include "yaagc_socket.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static void sim_sleep_10ms(void) { Sleep(10); }
#else
#include <time.h>
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

// agc_engine_init is upstream's, not our memory-loading variant.
extern int agc_engine_init(agc_t *State, const char *RomImage,
                           const char *CoreDump, int AllOrErasable);

// Engine cycle hooks that yaagc_socket exposes. Declarations match the
// canonical SocketAPI signatures so the engine's per-cycle calls (which
// look for ChannelInput/Output/Routine by name) can dispatch into them
// via these wrappers. Upstream agc_engine.c calls ChannelInput(),
// ChannelOutput(), ChannelRoutine() — define those here as the routing
// layer for this host build. (Production builds use io_callbacks.c.)
extern void yaagc_socket_channel_output(agc_t *State, int Channel, int Value);
extern int  yaagc_socket_channel_input (agc_t *State);
extern void yaagc_socket_channel_routine(agc_t *State);

void ChannelOutput(agc_t *State, int Channel, int Value)
{
    yaagc_socket_channel_output(State, Channel, Value);
}
int ChannelInput(agc_t *State)
{
    return yaagc_socket_channel_input(State);
}
void ChannelRoutine(agc_t *State)
{
    yaagc_socket_channel_routine(State);
}
void ShiftToDeda(agc_t *State, int Data) { (void)State; (void)Data; }
void RequestRadarData(agc_t *State) { (void)State; }

static agc_t state;
agc_t *agc_core_state(void) { return &state; }

int main(int argc, char **argv)
{
    const char *rom = getenv("ROM");
    if (!rom) rom = "../../build/roms/Luminary099.bin";
    uint16_t port = 19850;
    if (argc > 1) port = (uint16_t)atoi(argv[1]);

    int rc = agc_engine_init(&state, rom, NULL, 0);
    if (rc != 0) {
        fprintf(stderr, "agc_engine_init failed: %d (rom=%s)\n", rc, rom);
        return 1;
    }
    if (yaagc_socket_init(port) != 0) {
        fprintf(stderr, "yaagc_socket_init(%u) failed\n", (unsigned)port);
        return 2;
    }
    fprintf(stderr, "test_yaagc_socket_host: listening on 0.0.0.0:%u, ROM=%s\n",
            (unsigned)port, rom);

    // SimExecute pattern, same as agc_simulator.c::SimExecute (upstream
    // line 299). Real-time-paced 853-cycle bursts driven by sim_times().
    long real_time_offset = sim_times();
    long cycle_count = CLK_TCK * (long)state.CycleCounter;
    real_time_offset -= (cycle_count + AGC_PER_SECOND / 2) / AGC_PER_SECOND;
    long last_real_time = ~(long)0;
    long desired_cycles = 0;

    while (1) {
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
        }
    }
    return 0;
}
