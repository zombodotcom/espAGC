// Host-side shim for freertos/task.h. The macros channel_router.c uses
// (taskENTER_CRITICAL / taskEXIT_CRITICAL) are already defined as no-op
// inlines in FreeRTOS.h. This header just exists so the same `#include`
// the firmware does compiles on host.
#pragma once
#include "freertos/FreeRTOS.h"
