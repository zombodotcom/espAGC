// Host-side shim for FreeRTOS semaphores. We don't need real mutexes on
// host — tests are single-threaded — so this maps everything to no-ops.
#pragma once
#include "freertos/FreeRTOS.h"

typedef void *SemaphoreHandle_t;

static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    static int dummy;
    return &dummy;
}
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t t) {
    (void)s; (void)t; return pdTRUE;
}
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t s) {
    (void)s; return pdTRUE;
}
static inline SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    static int dummy2;
    return &dummy2;
}
