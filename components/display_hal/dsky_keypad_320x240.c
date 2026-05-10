// components/display_hal/dsky_keypad_320x240.c
//
// Keypad cell table + hit-test for the 320x240 DSKY layout. Pure logic,
// host-testable. The renderer in dsky_render_320x240.c iterates the same
// table to draw cells; this file owns the (col, row) -> key mapping.

#include "dsky_keypad_320x240.h"
#include "dsky_keys.h"
#include <stddef.h>

const dsky_kp_cell_t dsky_kp_cells_320x240[] = {
    { 0, 0, DSKY_KEY_VERB,   "V" },
    { 1, 0, DSKY_KEY_PLUS,   "+" },
    { 2, 0, DSKY_KEY_7,      "7" },
    { 3, 0, DSKY_KEY_8,      "8" },
    { 4, 0, DSKY_KEY_9,      "9" },
    { 5, 0, DSKY_KEY_CLR,    "C" },

    { 0, 1, DSKY_KEY_NOUN,   "N" },
    { 1, 1, DSKY_KEY_MINUS,  "-" },
    { 2, 1, DSKY_KEY_4,      "4" },
    { 3, 1, DSKY_KEY_5,      "5" },
    { 4, 1, DSKY_KEY_6,      "6" },
    { 5, 1, DSKY_KEY_PRO,    "P" },

    { 2, 2, DSKY_KEY_1,      "1" },
    { 3, 2, DSKY_KEY_2,      "2" },
    { 4, 2, DSKY_KEY_3,      "3" },
    { 5, 2, DSKY_KEY_KEYREL, "K" },

    { 1, 3, DSKY_KEY_0,      "0" },
    { 5, 3, DSKY_KEY_ENTR,   "E" },

    { 5, 4, DSKY_KEY_RSET,   "R" },
};
const int dsky_kp_cells_320x240_count =
    (int)(sizeof(dsky_kp_cells_320x240) / sizeof(dsky_kp_cells_320x240[0]));

int dsky_keypad_320x240_hit(int x, int y)
{
    if (x < DSKY_KP_X0 || x >= DSKY_KP_X1) return -1;
    if (y < DSKY_KP_Y0 || y >= DSKY_KP_Y1) return -1;
    int col = (x - DSKY_KP_X0) / DSKY_KP_CW;
    int row = (y - DSKY_KP_Y0) / DSKY_KP_CH;
    // The XPT2046 driver's map_range() clamps reported X to panel_w-1 = 319,
    // and (319 - DSKY_KP_X0) / DSKY_KP_CW = 6 — one past the rightmost valid
    // column. Pin col/row inside the table so taps in the bottom-right
    // sliver (RSET, ENTR) actually land on their cells instead of falling
    // through to "no hit".
    if (col >= DSKY_KP_COLS) col = DSKY_KP_COLS - 1;
    if (row >= DSKY_KP_ROWS) row = DSKY_KP_ROWS - 1;
    for (int i = 0; i < dsky_kp_cells_320x240_count; i++) {
        if (dsky_kp_cells_320x240[i].col == col &&
            dsky_kp_cells_320x240[i].row == row)
            return dsky_kp_cells_320x240[i].code;
    }
    return -1;
}
