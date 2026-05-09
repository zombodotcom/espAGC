#pragma once
//
// dsky_state_t — decoded view of the DSKY display panel as sourced from AGC
// channels 010 / 011 / 0163. The channel_router decodes raw channel writes
// into this struct; display_hal renders it. New display backends never touch
// raw AGC channels.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 7-segment digits. Negative = blank; 0-9 = lit digit. We don't use raw
// 7-seg patterns here — display_hal owns segment shapes — so the AGC's
// 5-bit illuminated-digit code is decoded back to a small int.
typedef int8_t dsky_digit_t;
#define DSKY_BLANK ((dsky_digit_t)-1)

typedef enum { DSKY_SIGN_NONE = 0, DSKY_SIGN_PLUS, DSKY_SIGN_MINUS } dsky_sign_t;

typedef struct {
    dsky_digit_t prog[2];        // 2-digit program number
    dsky_digit_t verb[2];        // 2-digit verb
    dsky_digit_t noun[2];        // 2-digit noun

    dsky_sign_t  r1_sign;
    dsky_digit_t r1[5];          // 5-digit register 1

    dsky_sign_t  r2_sign;
    dsky_digit_t r2[5];

    dsky_sign_t  r3_sign;
    dsky_digit_t r3[5];

    // Status indicators (channel 011/0163-driven).
    bool comp_acty;
    bool uplink_acty;
    bool temp;
    bool no_att;
    bool gimbal_lock;
    bool prog_alarm;
    bool restart;
    bool tracker;
    bool key_rel;
    bool opr_err;
    bool stby;
    bool flash_verb_noun;        // VERB/NOUN flash flag

    uint64_t generation;          // monotonically incremented on any change
} dsky_state_t;

#ifdef __cplusplus
}
#endif
