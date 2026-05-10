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

    case 12:
        // ch10 row 12 = "flag word". Per yaDSKY2/yaDSKY2.cpp Inds[] (lines
        // 181-211) several caution lights are latched here in addition to
        // the verb/noun flash bit. Direct assignment so the AGC's
        // explicit-clear actually clears the indicator.
        //   bit 0x008 (010 octal) : NO ATT
        //   bit 0x020 (040 octal) : flash V/N (also surfaces on ch011 040)
        //   bit 0x040 (0100 oct)  : GIMBAL LOCK
        //   bit 0x080 (0200 oct)  : TRACKER
        //   bit 0x100 (0400 oct)  : PROG ALARM
        g_snapshot.no_att          = (payload & 0x008) != 0;
        g_snapshot.flash_verb_noun = (payload & 0x020) != 0;
        g_snapshot.gimbal_lock     = (payload & 0x040) != 0;
        g_snapshot.tracker         = (payload & 0x080) != 0;
        g_snapshot.prog_alarm      = (payload & 0x100) != 0;
        break;
    default: break;
    }
    mark_dirty();
}

// Channel 011 indicator bits per yaDSKY2 Inds[]:
//   0x002 (02 oct)  : COMP ACTY (green dot)
//   0x004 (04 oct)  : UPLINK ACTY
//   0x008 (010 oct) : TEMP
//   0x040 (040 oct) : flash VERB/NOUN
static void apply_ch11(int payload)
{
    g_snapshot.comp_acty   = (payload & 0x002) != 0;
    g_snapshot.uplink_acty = (payload & 0x004) != 0;
    g_snapshot.temp        = (payload & 0x008) != 0;
    // ch11 040 also drives the V/N flash; keep latched along with the
    // ch10-row-12 source so either one lights it.
    if (payload & 0x040) g_snapshot.flash_verb_noun = true;
    mark_dirty();
}

// Channel 0163 (LM-only block-II caution and warning):
//   0x010 (020 oct)  : KEY REL
//   0x040 (0100 oct) : OPR ERR
//   0x080 (0200 oct) : RESTART
//   0x100 (0400 oct) : STBY
static void apply_ch163(int payload)
{
    g_snapshot.key_rel = (payload & 0x010) != 0;
    g_snapshot.opr_err = (payload & 0x040) != 0;
    g_snapshot.restart = (payload & 0x080) != 0;
    g_snapshot.stby    = (payload & 0x100) != 0;
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

// Diagnostic: log only channel-10 digit-row writes (rows 1..11) and the
// row-12 flag word. No count cap — we want to see post-tap echoes for as
// long as the user runs the firmware. Skips the high-volume telemetry
// channels (ch034/ch035 downlink, ch005/ch006 gyro counters) and the
// constant ch010 row=0 zero-relay refreshes.
static int g_last_ch10_row = -1;
static int g_last_ch10_payload = -1;
static int g_last_ch11 = -1;
static int g_last_ch163 = -1;

static void diag_log(int channel, int value)
{
    if (channel == 010) {
        int row = (value >> 11) & 0x0F;
        int payload = value & 0x07FF;
        if (row == 0 && payload == 0) return;        // skip the zero-relay spam
        if (row == g_last_ch10_row && payload == g_last_ch10_payload) return;
        g_last_ch10_row = row;
        g_last_ch10_payload = payload;
        ESP_LOGI(TAG, "ch010 row=%2d payload=%04o (left=%2d right=%2d)",
                 row, payload, (payload >> 5) & 0x1F, payload & 0x1F);
    } else if (channel == 011) {
        if (value == g_last_ch11) return;
        g_last_ch11 = value;
        ESP_LOGI(TAG, "ch011 value=%05o", value);
    } else if (channel == 0163) {
        if (value == g_last_ch163) return;
        g_last_ch163 = value;
        ESP_LOGI(TAG, "ch0163 value=%05o", value);
    }
    // everything else (ch034/ch035 telemetry, gyro, etc.) is silent.
}

// Periodic snapshot dump so we can see the resolved DSKY state without
// staring at raw channel writes. Called from channel_router_routine().
static int g_routine_count = 0;

void channel_router_on_output(int channel, int value)
{
    if (g_mutex == NULL) return;
    diag_log(channel, value);
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

void channel_router_on_routine(void)
{
    // The engine calls this once per ChannelRoutineCount tick (~every 02000
    // engine cycles). About every 256 calls (~5 s wall-time) dump the
    // resolved DSKY state to UART so we can see what the renderer would
    // paint, independent of the LCD itself. Keeps the channel-write log
    // skimmable.
    if (++g_routine_count % 256 != 0) return;

    char prog[3], verb[3], noun[3];
    char r1[7], r2[7], r3[7];
    #define DC(d) ((d) < 0 ? '_' : (char)('0' + (d)))
    #define SC(s) ((s) == DSKY_SIGN_PLUS ? '+' : (s) == DSKY_SIGN_MINUS ? '-' : ' ')
    prog[0]=DC(g_snapshot.prog[0]); prog[1]=DC(g_snapshot.prog[1]); prog[2]=0;
    verb[0]=DC(g_snapshot.verb[0]); verb[1]=DC(g_snapshot.verb[1]); verb[2]=0;
    noun[0]=DC(g_snapshot.noun[0]); noun[1]=DC(g_snapshot.noun[1]); noun[2]=0;
    r1[0]=SC(g_snapshot.r1_sign); for(int i=0;i<5;i++) r1[1+i]=DC(g_snapshot.r1[i]); r1[6]=0;
    r2[0]=SC(g_snapshot.r2_sign); for(int i=0;i<5;i++) r2[1+i]=DC(g_snapshot.r2[i]); r2[6]=0;
    r3[0]=SC(g_snapshot.r3_sign); for(int i=0;i<5;i++) r3[1+i]=DC(g_snapshot.r3[i]); r3[6]=0;
    #undef DC
    #undef SC
    ESP_LOGI(TAG, "snap PRG=%s VRB=%s NUN=%s R1=%s R2=%s R3=%s "
                  "ca=%d up=%d pa=%d oe=%d",
             prog, verb, noun, r1, r2, r3,
             g_snapshot.comp_acty, g_snapshot.uplink_acty,
             g_snapshot.prog_alarm, g_snapshot.opr_err);

    // Read agc_engine internal watchdog flags directly. If any of these
    // are non-zero post-boot, the engine entered an alarm state which is
    // why the digit display stays blank. Per Layer-2 host tests
    // (tests/host/test_alarm_at_boot), our Luminary099 boot trips
    // NightWatchman + WarningFilter very early and stays there.
    extern agc_t *agc_core_state(void);
    agc_t *st = agc_core_state();
    // CycleCounter is a `uint64_t` declared in the engine but its bit
    // layout depends on `__embedded__` and packing; printing the low 32
    // bits avoids junk on platforms where the cast is unstable.
    uint32_t cyc_lo = (uint32_t)(st->CycleCounter & 0xFFFFFFFFu);
    ESP_LOGI(TAG, "alarms RuptLock=%d NW=%d TC=%d NoTC=%d PF=%d WF=%d GW=%d  "
                  "RegZ=%05o cyc_lo=%u",
             (int)st->RuptLock, (int)st->NightWatchmanTripped,
             (int)st->TCTrap, (int)st->NoTC, (int)st->ParityFail,
             (int)st->WarningFilter, (int)st->GeneratedWarning,
             (unsigned)st->Erasable[0][RegZ],
             (unsigned)cyc_lo);
}

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
