// display_hal.c — direct-to-framebuffer DSKY renderer.
//
// Owns a 160x80 RGB565 framebuffer that gets composed each frame from the
// dsky_state_t snapshot and pushed to the ST7735 panel. No LVGL — keeps the
// firmware self-contained and avoids the broken managed components on this
// chip / panel combination.
//
// Layout (15 cells across, 11 rows of 7px each):
//   y= 0..7   "ESPAGC"          (boot splash, redrawn every frame so the
//                                presence of the title doubles as a heartbeat)
//   y=10..17  "PRG  XX  VRB XX"
//   y=20..27  "NUN  XX  PRO P*"  (PRO is just an indicator)
//   y=32..39  "R1  +XXXXX"
//   y=42..49  "R2  +XXXXX"
//   y=52..59  "R3  +XXXXX"
//   y=64..71  "CA UP PA OE SBY" (only lit indicators)

#include "display_hal.h"
#include "st7735_panel.h"
#include "font5x7.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "dsky";
static uint16_t *fb;        // DSKY_FB_W * DSKY_FB_H pixels, allocated in PSRAM

#define COL_BG    0x0000      // black
#define COL_AMBER 0xFD20      // 7-seg amber
#define COL_DIM   0x4208      // dim grey
#define COL_RED   0xF800
#define COL_GREEN 0x07E0

static void put_pixel(int x, int y, uint16_t c)
{
    if ((unsigned)x < DSKY_FB_W && (unsigned)y < DSKY_FB_H) fb[y * DSKY_FB_W + x] = c;
}

static void draw_glyph(int x0, int y0, int idx, uint16_t c)
{
    const uint8_t *g = font5x7[idx];
    for (int col = 0; col < FONT_W; col++) {
        uint8_t bits = g[col];
        for (int row = 0; row < FONT_H; row++) {
            if (bits & (1 << row)) put_pixel(x0 + col, y0 + row, c);
        }
    }
}

static void draw_text(int x0, int y0, const char *s, uint16_t c)
{
    int x = x0;
    for (; *s; s++) {
        draw_glyph(x, y0, font_index(*s), c);
        x += FONT_W + 1;       // 1px column gap
    }
}

static char digit_char(int8_t d) { return d < 0 ? ' ' : (char)('0' + d); }
static char sign_char (int s)    {
    return s == DSKY_SIGN_PLUS ? '+' : s == DSKY_SIGN_MINUS ? '-' : ' ';
}

void display_hal_init(void)
{
    fb = heap_caps_malloc(DSKY_FB_W * DSKY_FB_H * sizeof(uint16_t),
                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!fb) {
        // Fall back to internal RAM. 25 KB is feasible.
        fb = heap_caps_malloc(DSKY_FB_W * DSKY_FB_H * sizeof(uint16_t),
                              MALLOC_CAP_8BIT);
    }
    if (!fb) { ESP_LOGE(TAG, "fb alloc failed"); return; }
    memset(fb, 0, DSKY_FB_W * DSKY_FB_H * sizeof(uint16_t));

    if (st7735_init() != ESP_OK) {
        ESP_LOGE(TAG, "st7735_init failed");
        return;
    }

    // Splash so we know the panel is alive even before the engine emits.
    draw_text(2, 2, "ESPAGC", COL_AMBER);
    draw_text(2, 12, "BOOTING", COL_DIM);
    st7735_draw_rows(0, DSKY_FB_H, fb);
    ESP_LOGI(TAG, "panel splash drawn");
}

void display_hal_update(const dsky_state_t *s)
{
    if (!fb) return;

    memset(fb, 0, DSKY_FB_W * DSKY_FB_H * sizeof(uint16_t));

    char line[32];

    draw_text(2, 0, "ESPAGC", COL_AMBER);

    snprintf(line, sizeof line, "PRG %c%c  VRB %c%c",
             digit_char(s->prog[0]), digit_char(s->prog[1]),
             digit_char(s->verb[0]), digit_char(s->verb[1]));
    draw_text(2, 10, line, COL_AMBER);

    snprintf(line, sizeof line, "NUN %c%c", digit_char(s->noun[0]), digit_char(s->noun[1]));
    draw_text(2, 20, line, COL_AMBER);

    snprintf(line, sizeof line, "R1 %c%c%c%c%c%c",
             sign_char(s->r1_sign),
             digit_char(s->r1[0]), digit_char(s->r1[1]),
             digit_char(s->r1[2]), digit_char(s->r1[3]), digit_char(s->r1[4]));
    draw_text(2, 32, line, COL_AMBER);

    snprintf(line, sizeof line, "R2 %c%c%c%c%c%c",
             sign_char(s->r2_sign),
             digit_char(s->r2[0]), digit_char(s->r2[1]),
             digit_char(s->r2[2]), digit_char(s->r2[3]), digit_char(s->r2[4]));
    draw_text(2, 42, line, COL_AMBER);

    snprintf(line, sizeof line, "R3 %c%c%c%c%c%c",
             sign_char(s->r3_sign),
             digit_char(s->r3[0]), digit_char(s->r3[1]),
             digit_char(s->r3[2]), digit_char(s->r3[3]), digit_char(s->r3[4]));
    draw_text(2, 52, line, COL_AMBER);

    int x = 2;
    if (s->comp_acty)   { draw_text(x, 64, "CA",  COL_GREEN); x += 18; }
    if (s->uplink_acty) { draw_text(x, 64, "UP",  COL_GREEN); x += 18; }
    if (s->prog_alarm)  { draw_text(x, 64, "PA",  COL_RED);   x += 18; }
    if (s->opr_err)     { draw_text(x, 64, "OE",  COL_RED);   x += 18; }
    if (s->stby)        { draw_text(x, 64, "SBY", COL_DIM);   x += 24; }

    st7735_draw_rows(0, DSKY_FB_H, fb);
}
