// agc_init.c
//
// Replaces upstream agc_engine_init.c so that we can load a ROM directly out of
// flash-resident memory instead of through stdio. The CPU-state initialisation
// below is structurally identical to the upstream sequence (see
// third_party/virtualagc/yaAGC/agc_engine_init.c, GPL v2) so behaviour matches
// what the engine expects on first call.

#include "agc_core.h"

#include <string.h>

#include "yaAGC.h"
#include "agc_engine.h"

// Engine internal globals declared extern in agc_engine.h. yaAGC normally
// defines this in agc_engine_init.c — we hold it here instead.
int initializeSunburst37 = 0;

// Single static AGC state. yaAGC was designed for one engine per process and
// peppers the codebase with that assumption.
static agc_t g_state;
agc_t *agc_core_state(void) { return &g_state; }

static int load_rom_from_mem(agc_t *State, const uint8_t *Rom, size_t RomSize)
{
    if (State == NULL) return AGC_ERR_NO_STATE;
    if (RomSize & 1)   return AGC_ERR_ROM_ODD;

    int n = (int)(RomSize / 2);             // 16-bit word count
    if (n > 36 * 02000) return AGC_ERR_ROM_TOO_BIG;

    State->CheckParity = 0;
    memset(&State->Parities, 0, sizeof(State->Parities));

    // Banks in the file are stored in order 2,3,0,1,4,5,...,35.
    int Bank = 2, j = 0;
    for (int i = 0; i < n; i++) {
        uint8_t  b0 = Rom[2*i + 0];
        uint8_t  b1 = Rom[2*i + 1];
        uint16_t Raw = (uint16_t)b0 * 256u + (uint16_t)b1;
        uint8_t  Parity = Raw & 1;

        if (Bank > 35) return AGC_ERR_ROM_TOO_BIG;
        State->Fixed[Bank][j] = Raw >> 1;
        State->Parities[(Bank * 02000 + j) / 32] |= (uint32_t)Parity << (j % 32);
        j++;
        if (Parity) State->CheckParity = 1;

        if (j == 02000) {
            j = 0;
            if      (Bank == 2) Bank = 3;
            else if (Bank == 3) Bank = 0;
            else if (Bank == 0) Bank = 1;
            else if (Bank == 1) Bank = 4;
            else                Bank++;
        }
    }
    return AGC_OK;
}

static void init_cpu_state(agc_t *State)
{
    // Clear i/o channels.
    for (int i = 0; i < NUM_CHANNELS; i++) State->InputChannel[i] = 0;
    State->InputChannel[030] = 037777;
    State->InputChannel[031] = 077777;
    State->InputChannel[032] = 077777;
    State->InputChannel[033] = 077777;

    // Clear erasable memory.
    for (int Bank = 0; Bank < 8; Bank++)
        for (int j = 0; j < 0400; j++)
            State->Erasable[Bank][j] = 0;
    State->Erasable[0][RegZ] = 04000;       // Initial PC.

    State->CycleCounter = 0;
    State->ExtraCode = 0;
    State->AllowInterrupt = 1;
    State->InterruptRequests[8] = 1;        // DOWNRUPT.
    State->PendFlag = 0;
    State->PendDelay = 0;
    State->ExtraDelay = 0;

    State->OutputChannel7 = 0;
    for (int j = 0; j < 16; j++) State->OutputChannel10[j] = 0;
    State->IndexValue = 0;
    for (int j = 0; j < 1 + NUM_INTERRUPT_TYPES; j++) State->InterruptRequests[j] = 0;
    State->InIsr = 0;
    State->SubstituteInstruction = 0;
    State->DownruptTimeValid = 1;
    State->DownruptTime = 0;
    State->Downlink = 0;

    State->NightWatchman = 0;
    State->NightWatchmanTripped = 0;
    State->RuptLock = 0;
    State->NoRupt = 0;
    State->TCTrap = 0;
    State->NoTC = 0;
    State->ParityFail = 0;

    State->WarningFilter = 0;
    State->GeneratedWarning = 0;

    State->RestartLight = 0;
    State->Standby = 0;
    State->SbyPressed = 0;
    State->SbyStillPressed = 0;

    State->NextZ = 0;
    State->ScalerCounter = 0;
    State->ChannelRoutineCount = 0;

    State->DskyTimer = 0;
    State->DskyFlash = 0;
    State->DskyChannel163 = 0;

    State->TookBZF = 0;
    State->TookBZMF = 0;

    State->Trap31A = 0;
    State->Trap31B = 0;
    State->Trap32 = 0;

    State->RadarGateCounter = 0;
}

agc_status_t agc_core_init(const uint8_t *rom, size_t rom_size)
{
    memset(&g_state, 0, sizeof(g_state));
    init_cpu_state(&g_state);
    int rc = load_rom_from_mem(&g_state, rom, rom_size);
    return (agc_status_t)rc;
}

int agc_core_step(int batch_size)
{
    int n = 0;
    while (n < batch_size) {
        // agc_engine() returns 0 on a successful machine cycle.
        int r = agc_engine(&g_state);
        (void)r;
        n++;
    }
    return n;
}

void agc_core_force_dsky_refresh(void)
{
    // Set OutputChannel10 entries to "stale" sentinels so the next engine pass
    // re-emits the ChannelOutput callbacks. yaAGC stores the last latched value
    // here; clearing forces re-issue on next write.
    for (int j = 0; j < 16; j++) g_state.OutputChannel10[j] = 0;
    g_state.DskyChannel163 = 0;
}
