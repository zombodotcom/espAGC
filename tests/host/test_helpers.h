#pragma once
//
// Minimal test harness — same shape as dosNew/esp-dos/tests/emu/test_helpers.h.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define ASSERT(cond, ...) do {                                               \
    if (!(cond)) {                                                           \
        fprintf(stderr, "FAIL %s:%d: " #cond " — ", __FILE__, __LINE__);     \
        fprintf(stderr, __VA_ARGS__);                                        \
        fputc('\n', stderr);                                                 \
        exit(1);                                                             \
    }                                                                        \
} while (0)

#define PASS() do { printf("PASS\n"); return 0; } while (0)

// Loads the Luminary099.bin path from $ROM (set by the Makefile). Returns
// malloc'd buffer; caller must free.
static inline uint8_t *load_rom_file(size_t *out_size)
{
    const char *path = getenv("ROM");
    if (!path || !*path) { fputs("ROM env not set\n", stderr); exit(1); }
    FILE *fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "open %s: %s\n", path, strerror(errno)); exit(1); }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc(sz);
    if (fread(buf, 1, sz, fp) != (size_t)sz) { fputs("short read\n", stderr); exit(1); }
    fclose(fp);
    *out_size = sz;
    return buf;
}
