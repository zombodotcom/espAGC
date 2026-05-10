// Host-side shim for FreeRTOS.h. Just the symbols channel_router.c needs.
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;

#define portMAX_DELAY ((TickType_t)0xFFFFFFFF)
#define pdTRUE  ((BaseType_t)1)
#define pdFALSE ((BaseType_t)0)
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

static inline void vTaskDelay(TickType_t t) { (void)t; }
