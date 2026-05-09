// components/display_hal/dsky_render_320x240.c
//
// 320x240 DSKY layout for the CYD-2432S028C. Faithful to the canonical
// Apollo DSKY: status panel left (60 px), display window upper-right
// (256x96), 19-key touch keypad lower-right (256x140, 6 cols x 5 rows).
//
// Rendered in three 80-row strips to fit the original ESP32's internal
// SRAM. strip_h = 80; the renderer only draws elements that intersect
// the current strip.

#include "dsky_layout.h"
#include "dsky_keypad_320x240.h"
#include "font5x7.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define FB_W       320
#define FB_H       240
#define STRIP_H     80

#define COL_BG       0x0841
#define COL_AMBER    0xFD20
#define COL_AMBER_D  0x7A00
#define COL_GREEN    0x07E0
#define COL_RED      0xF800
#define COL_DIM      0x4208
#define COL_LIT_W    0xEF7D
#define COL_LIT_Y    0xFEA0
#define COL_PANEL    0x18C3

// --- pixel helpers (strip-local y) ------------------------------------

static void put_pixel_strip(uint16_t *strip, int x, int y_local, uint16_t c)
{
    if ((unsigned)x < FB_W && (unsigned)y_local < STRIP_H)
        strip[y_local * FB_W + x] = c;
}

static void fill_rect_strip(uint16_t *strip, int x, int y_local, int w, int h, uint16_t c)
{
    for (int yy = 0; yy < h; yy++) {
        int yl = y_local + yy;
        if ((unsigned)yl >= STRIP_H) continue;
        for (int xx = 0; xx < w; xx++) {
            int xc = x + xx;
            if ((unsigned)xc >= FB_W) continue;
            strip[yl * FB_W + xc] = c;
        }
    }
}

static void draw_glyph_strip(uint16_t *strip, int x0, int y0_local, int idx, uint16_t c)
{
    const uint8_t *g = font5x7[idx];
    for (int col = 0; col < FONT_W; col++) {
        uint8_t bits = g[col];
        for (int row = 0; row < FONT_H; row++) {
            if (bits & (1 << row)) put_pixel_strip(strip, x0 + col, y0_local + row, c);
        }
    }
}

static void draw_text_strip(uint16_t *strip, int x0, int y0_local, const char *s, uint16_t c)
{
    int x = x0, y = y0_local;
    for (; *s; s++) {
        if (*s == '\n') { x = x0; y += FONT_H + 1; continue; }
        draw_glyph_strip(strip, x, y, font_index(*s), c);
        x += FONT_W + 1;
    }
}

// --- status panel -----------------------------------------------------

#define SP_W        60
#define SP_CELL_W   28
#define SP_CELL_H   33
#define SP_GAP_X     2

typedef struct { int row; const char *text; bool is_yellow; int flag_offset; } sp_cell_t;
#define FLAG(field) ((int)offsetof(dsky_state_t, field))

static const sp_cell_t sp_cells[] = {
    { 0, "UPLINK\nACTY",  false, FLAG(uplink_acty) },
    { 1, "NO ATT",        false, FLAG(no_att) },
    { 2, "STBY",          false, FLAG(stby) },
    { 3, "KEY REL",       false, FLAG(key_rel) },
    { 4, "OPR ERR",       false, FLAG(opr_err) },

    { 0, "TEMP",          true,  FLAG(temp) },
    { 1, "GIMBAL\nLOCK",  true,  FLAG(gimbal_lock) },
    { 2, "PROG",          true,  FLAG(prog_alarm) },
    { 3, "RESTART",       true,  FLAG(restart) },
    { 4, "TRACKER",       true,  FLAG(tracker) },
    { 5, "ALT",           true,  -1 },
    { 6, "VEL",           true,  -1 },
};
#define SP_CELL_COUNT (int)(sizeof(sp_cells) / sizeof(sp_cells[0]))

static void render_status_panel(uint16_t *strip, const dsky_state_t *s, int strip_y0)
{
    fill_rect_strip(strip, 0, 0 - strip_y0, SP_W, FB_H, COL_PANEL);

    for (int i = 0; i < SP_CELL_COUNT; i++) {
        const sp_cell_t *c = &sp_cells[i];
        int col = c->is_yellow ? 1 : 0;
        int cx  = col * (SP_CELL_W + SP_GAP_X);
        int cy  = c->row * SP_CELL_H;
        int cyl = cy - strip_y0;
        if (cyl + SP_CELL_H <= 0 || cyl >= STRIP_H) continue;

        bool lit = false;
        if (c->flag_offset >= 0) {
            const uint8_t *base = (const uint8_t *)s;
            lit = *(const bool *)(base + c->flag_offset);
        }
        uint16_t bg = lit ? (c->is_yellow ? COL_LIT_Y : COL_LIT_W) : COL_DIM;
        uint16_t fg = lit ? 0x0000 : (c->is_yellow ? COL_AMBER_D : 0x6B6D);

        fill_rect_strip(strip, cx + 1, cyl + 1, SP_CELL_W - 2, SP_CELL_H - 2, bg);
        draw_text_strip(strip, cx + 3, cyl + 4, c->text, fg);
    }
}

// --- register window --------------------------------------------------

#define RW_X0   64
#define RW_W   256
#define RW_H    96

static char digit_char(int8_t d) { return d < 0 ? ' ' : (char)('0' + d); }
static char sign_char (int s)    {
    return s == DSKY_SIGN_PLUS ? '+' : s == DSKY_SIGN_MINUS ? '-' : ' ';
}

static void render_register_window(uint16_t *strip, const dsky_state_t *s, int strip_y0)
{
    fill_rect_strip(strip, RW_X0, 0 - strip_y0, RW_W, RW_H, 0x0000);

    char line[16];

    if (s->comp_acty)
        fill_rect_strip(strip, RW_X0 + 4, 4 - strip_y0, 6, 6, COL_GREEN);
    draw_text_strip(strip, RW_X0 + 14, 4 - strip_y0, "COMP\nACTY", COL_GREEN);

    draw_text_strip(strip, RW_X0 + 200, 4 - strip_y0, "PROG", COL_AMBER_D);
    snprintf(line, sizeof line, "%c%c", digit_char(s->prog[0]), digit_char(s->prog[1]));
    draw_text_strip(strip, RW_X0 + 232, 4 - strip_y0, line, COL_AMBER);

    draw_text_strip(strip, RW_X0 + 14, 22 - strip_y0, "VERB", COL_AMBER_D);
    snprintf(line, sizeof line, "%c%c", digit_char(s->verb[0]), digit_char(s->verb[1]));
    draw_text_strip(strip, RW_X0 + 50, 22 - strip_y0, line, COL_AMBER);

    draw_text_strip(strip, RW_X0 + 130, 22 - strip_y0, "NOUN", COL_AMBER_D);
    snprintf(line, sizeof line, "%c%c", digit_char(s->noun[0]), digit_char(s->noun[1]));
    draw_text_strip(strip, RW_X0 + 166, 22 - strip_y0, line, COL_AMBER);

    snprintf(line, sizeof line, "R1 %c%c%c%c%c%c", sign_char(s->r1_sign),
             digit_char(s->r1[0]), digit_char(s->r1[1]),
             digit_char(s->r1[2]), digit_char(s->r1[3]), digit_char(s->r1[4]));
    draw_text_strip(strip, RW_X0 + 14, 42 - strip_y0, line, COL_AMBER);

    snprintf(line, sizeof line, "R2 %c%c%c%c%c%c", sign_char(s->r2_sign),
             digit_char(s->r2[0]), digit_char(s->r2[1]),
             digit_char(s->r2[2]), digit_char(s->r2[3]), digit_char(s->r2[4]));
    draw_text_strip(strip, RW_X0 + 14, 60 - strip_y0, line, COL_AMBER);

    snprintf(line, sizeof line, "R3 %c%c%c%c%c%c", sign_char(s->r3_sign),
             digit_char(s->r3[0]), digit_char(s->r3[1]),
             digit_char(s->r3[2]), digit_char(s->r3[3]), digit_char(s->r3[4]));
    draw_text_strip(strip, RW_X0 + 14, 78 - strip_y0, line, COL_AMBER);
}

// --- keypad render (hit-test + cell table live in dsky_keypad_320x240.c)

static void render_keypad(uint16_t *strip, int strip_y0)
{
    fill_rect_strip(strip, DSKY_KP_X0, DSKY_KP_Y0 - strip_y0,
                    DSKY_KP_X1 - DSKY_KP_X0, DSKY_KP_Y1 - DSKY_KP_Y0, 0x0000);

    for (int i = 0; i < dsky_kp_cells_320x240_count; i++) {
        const dsky_kp_cell_t *c = &dsky_kp_cells_320x240[i];
        int x0 = DSKY_KP_X0 + c->col * DSKY_KP_CW + 2;
        int y0 = DSKY_KP_Y0 + c->row * DSKY_KP_CH + 2;
        int w  = DSKY_KP_CW - 4;
        int h  = DSKY_KP_CH - 4;
        int yl = y0 - strip_y0;
        if (yl + h <= 0 || yl >= STRIP_H) continue;
        fill_rect_strip(strip, x0, yl, w, h, COL_PANEL);
        int lx = x0 + (w - FONT_W) / 2;
        int ly = y0 + (h - FONT_H) / 2 - strip_y0;
        draw_text_strip(strip, lx, ly, c->label, COL_AMBER);
    }
}

// --- iface entry points -----------------------------------------------

static void init_strip(uint16_t *strip, int y0)
{
    memset(strip, 0, FB_W * STRIP_H * sizeof(uint16_t));
    if (y0 == 0) {
        draw_text_strip(strip, 90, 30, "ESPAGC",  COL_AMBER);
        draw_text_strip(strip, 90, 50, "BOOTING", COL_DIM);
    }
}

static void render_strip(uint16_t *strip, const dsky_state_t *s, int y0)
{
    memset(strip, 0, FB_W * STRIP_H * sizeof(uint16_t));
    render_status_panel(strip, s, y0);
    if (y0 < RW_H)                  render_register_window(strip, s, y0);
    if (y0 + STRIP_H > DSKY_KP_Y0)  render_keypad(strip, y0);
}

static int hit_test(int x, int y) { return dsky_keypad_320x240_hit(x, y); }

const dsky_layout_t dsky_layout_320x240 = {
    .fb_w         = FB_W,
    .fb_h         = FB_H,
    .strip_h      = STRIP_H,
    .init_strip   = init_strip,
    .render_strip = render_strip,
    .hit_test     = hit_test,
};
