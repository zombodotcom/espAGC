// channel_router.c
//
// Decodes AGC output-channel writes into a dsky_state_t snapshot and queues
// keypresses bound for input channel 015. Single mutex protects the snapshot;
// a small lock-free ringbuffer carries keypresses from input transports into
// the engine task.

#include "channel_router.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "yaAGC.h"
#include "agc_engine.h"

static const char *TAG = "chrouter";

// Channel 010 is row-addressed: 4 high bits = row index (1..14), 11 low bits =
// row payload. Rows decode to display fields per the AGC IO assignment table.
//   Row 11: R1D1 R1D2
//   Row 10: R1D3 R1D4
//   Row 9 : R1D5 + R2D1 (sign in row payload)
//   Row 8 : R2D2 R2D3
//   Row 7 : R2D4 R2D5
//   Row 6 : R3D1 R3D2 (sign)
//   Row 5 : R3D3 R3D4
//   Row 4 : R3D5
//   Row 3 : NOUN1 NOUN2
//   Row 2 : VERB1 VERB2
//   Row 1 : PROG1 PROG2
//   Row 12: flags (flash, etc.)
//   Row 13: indicators (test)
// (See Virtual AGC docs for exact bit layout — we partially implement it.)
//
// Each two-digit field uses 4-bit "5-bit-illumination" encoding per digit.
// We map 0..9 directly; everything else is treated as blank.

static dsky_state_t  g_snapshot;
static SemaphoreHandle_t g_mutex;

#define KEY_RING_SZ 32
static volatile uint8_t  g_key_ring[KEY_RING_SZ];
static volatile uint16_t g_key_head;     // producer (input transports)
static volatile uint16_t g_key_tail;     // consumer (engine task)

static dsky_digit_t decode_digit(int code5)
{
    // AGC 5-bit illuminated digit code -> 0..9 or blank. Same table the
    // canonical yaDSKY2 uses (SevenSegmentFilenames[]). Anything outside
    // this set renders blank.
    switch (code5) {
        case 21: return 0;   case  3: return 1;   case 25: return 2;
        case 27: return 3;   case 15: return 4;   case 30: return 5;
        case 28: return 6;   case 19: return 7;   case 29: return 8;
        case 31: return 9;
        default: return DSKY_BLANK;
    }
}

static void mark_dirty(void) { g_snapshot.generation++; }

// Per-register sign latches. AGC ch10 sends + and - bits in two separate
// row payloads per register; we merge them and let the renderer decide.
static uint8_t r1_sign_bits, r2_sign_bits, r3_sign_bits;  // bit0=minus, bit1=plus

static dsky_sign_t resolve_sign(uint8_t bits)
{
    if (bits & 2) return DSKY_SIGN_PLUS;     // + has priority if both set
    if (bits & 1) return DSKY_SIGN_MINUS;
    return DSKY_SIGN_NONE;
}

static void apply_row(int row, int payload)
{
    // yaDSKY2 reference: yaDSKY2/yaDSKY2.cpp ~line 1953 onward. The relay
    // row index encoded in bits 14..11 of channel-10 maps to specific DSKY
    // fields below. The "left" digit lives in payload bits 9..5, "right"
    // in bits 4..0; bit 10 carries the sign for rows that have one.
    int left  = (payload >> 5) & 0x1F;
    int right = (payload >> 0) & 0x1F;
    bool sign_bit = (payload & (1 << 10)) != 0;

    switch (row) {
    case 11: g_snapshot.prog[0] = decode_digit(left);
             g_snapshot.prog[1] = decode_digit(right); break;
    case 10: g_snapshot.verb[0] = decode_digit(left);
             g_snapshot.verb[1] = decode_digit(right); break;
    case  9: g_snapshot.noun[0] = decode_digit(left);
             g_snapshot.noun[1] = decode_digit(right); break;

    case  8: g_snapshot.r1[0] = decode_digit(right); break;             // R1D1 only
    case  7: g_snapshot.r1[1] = decode_digit(left);                      // R1D2,R1D3,+
             g_snapshot.r1[2] = decode_digit(right);
             r1_sign_bits = (r1_sign_bits & ~2) | (sign_bit ? 2 : 0);
             g_snapshot.r1_sign = resolve_sign(r1_sign_bits); break;
    case  6: g_snapshot.r1[3] = decode_digit(left);                      // R1D4,R1D5,-
             g_snapshot.r1[4] = decode_digit(right);
             r1_sign_bits = (r1_sign_bits & ~1) | (sign_bit ? 1 : 0);
             g_snapshot.r1_sign = resolve_sign(r1_sign_bits); break;

    case  5: g_snapshot.r2[0] = decode_digit(left);                      // R2D1,R2D2,+
             g_snapshot.r2[1] = decode_digit(right);
             r2_sign_bits = (r2_sign_bits & ~2) | (sign_bit ? 2 : 0);
             g_snapshot.r2_sign = resolve_sign(r2_sign_bits); break;
    case  4: g_snapshot.r2[2] = decode_digit(left);                      // R2D3,R2D4,-
             g_snapshot.r2[3] = decode_digit(right);
             r2_sign_bits = (r2_sign_bits & ~1) | (sign_bit ? 1 : 0);
             g_snapshot.r2_sign = resolve_sign(r2_sign_bits); break;

    case  3: g_snapshot.r2[4] = decode_digit(left);                      // R2D5, R3D1
             g_snapshot.r3[0] = decode_digit(right); break;
    case  2: g_snapshot.r3[1] = decode_digit(left);                      // R3D2,R3D3,+
             g_snapshot.r3[2] = decode_digit(right);
             r3_sign_bits = (r3_sign_bits & ~2) | (sign_bit ? 2 : 0);
             g_snapshot.r3_sign = resolve_sign(r3_sign_bits); break;
    case  1: g_snapshot.r3[3] = decode_digit(left);                      // R3D4,R3D5,-
             g_snapshot.r3[4] = decode_digit(right);
             r3_sign_bits = (r3_sign_bits & ~1) | (sign_bit ? 1 : 0);
             g_snapshot.r3_sign = resolve_sign(r3_sign_bits); break;

    case 12: g_snapshot.flash_verb_noun = (payload & 0x20) != 0; break;
    default: break;
    }
    mark_dirty();
}

static void apply_ch11(int payload)
{
    // Subset of channel 011 status lights.
    g_snapshot.comp_acty   = (payload & (1 <<  1)) != 0;
    g_snapshot.uplink_acty = (payload & (1 <<  2)) != 0;
    g_snapshot.temp        = (payload & (1 <<  3)) != 0;
    g_snapshot.key_rel     = (payload & (1 <<  4)) != 0;
    g_snapshot.flash_verb_noun |= (payload & (1 << 5)) != 0;
    g_snapshot.opr_err     = (payload & (1 <<  6)) != 0;
    mark_dirty();
}

static void apply_ch163(int payload)
{
    g_snapshot.restart = (payload & (1 << 1)) != 0;
    g_snapshot.stby    = (payload & (1 << 4)) != 0;
    mark_dirty();
}

void channel_router_init(void)
{
    memset(&g_snapshot, 0, sizeof(g_snapshot));
    for (int i = 0; i < 5; i++) {
        g_snapshot.r1[i] = g_snapshot.r2[i] = g_snapshot.r3[i] = DSKY_BLANK;
    }
    g_snapshot.prog[0] = g_snapshot.prog[1] = DSKY_BLANK;
    g_snapshot.verb[0] = g_snapshot.verb[1] = DSKY_BLANK;
    g_snapshot.noun[0] = g_snapshot.noun[1] = DSKY_BLANK;

    if (g_mutex == NULL) g_mutex = xSemaphoreCreateMutex();
}

void channel_router_on_output(int channel, int value)
{
    if (g_mutex == NULL) return;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    switch (channel) {
    case 010: {
        int row = (value >> 11) & 0x0F;
        int payload = value & 0x07FF;
        apply_row(row, payload);
        break;
    }
    case 011: apply_ch11(value);  break;
    case 0163: apply_ch163(value); break;
    default: break; // ignore other channels for now
    }
    xSemaphoreGive(g_mutex);
}

int channel_router_pump_input(void *agc_state)
{
    agc_t *state = (agc_t *)agc_state;
    while (g_key_tail != g_key_head) {
        uint8_t code = g_key_ring[g_key_tail % KEY_RING_SZ];
        g_key_tail++;
        // Channel 015 input: 5-bit keycode. Raise interrupt 5 (KEYRUPT1).
        state->InputChannel[015] = code & 037;
        state->InterruptRequests[5] = 1;
        return 0;       // process one key per engine call
    }
    return 0;
}

void channel_router_on_routine(void) { /* nothing scheduled yet */ }

void channel_router_post_key(int code)
{
    uint16_t next = g_key_head + 1;
    if ((uint16_t)(next - g_key_tail) > KEY_RING_SZ) {
        ESP_LOGW(TAG, "key ring full, dropping %d", code);
        return;
    }
    g_key_ring[g_key_head % KEY_RING_SZ] = (uint8_t)(code & 0x1F);
    g_key_head = next;
}

uint64_t channel_router_snapshot(dsky_state_t *out)
{
    if (g_mutex == NULL || out == NULL) return 0;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    *out = g_snapshot;
    uint64_t gen = g_snapshot.generation;
    xSemaphoreGive(g_mutex);
    return gen;
}
