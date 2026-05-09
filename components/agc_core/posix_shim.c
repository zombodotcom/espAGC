// posix_shim.c
//
// Tiny shims for stdlib calls that engine helpers reach for on ports that
// don't have the desktop POSIX surface. ESP-IDF's newlib already provides
// random(), time(), gettimeofday() etc., so this file is intentionally sparse.
// Add shims here only when the linker complains.

#include <stdint.h>
#include <stddef.h>

// yaAGC's Backtrace.c references this debug global — provide a definition
// here in case the upstream's would-be definer (agc_simulator.c) is excluded.
int DebugMode_default_zero = 0;
