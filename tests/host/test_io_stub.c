// Host-side replacement for components/agc_core/io_callbacks.c. Stores every
// ChannelOutput call into a circular buffer that tests can inspect.

#include <errno.h>
#include "yaAGC.h"
#include "agc_engine.h"

#define IO_LOG_SZ 1024

typedef struct { int channel; int value; } io_log_entry_t;
io_log_entry_t io_log[IO_LOG_SZ];
int io_log_count = 0;

void ChannelOutput(agc_t *State, int Channel, int Value)
{
    (void)State;
    if (io_log_count < IO_LOG_SZ) {
        io_log[io_log_count].channel = Channel;
        io_log[io_log_count].value   = Value;
        io_log_count++;
    }
}

int  ChannelInput   (agc_t *State)            { (void)State; return 0; }
void ChannelRoutine (agc_t *State)            { (void)State; }
void ShiftToDeda    (agc_t *State, int Data)  { (void)State; (void)Data; }
void RequestRadarData(agc_t *State)           { (void)State; }
