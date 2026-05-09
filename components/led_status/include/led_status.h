#pragma once
#include "dsky_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void led_status_init(void);
// Drives the APA102 from a DSKY snapshot — green = COMP ACTY, blue = uplink,
// red = alarm/restart, off otherwise.
void led_status_update(const dsky_state_t *state);

#ifdef __cplusplus
}
#endif
