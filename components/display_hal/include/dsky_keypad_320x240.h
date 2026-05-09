#pragma once
//
// Keypad geometry for the 320x240 DSKY layout. Shared between the renderer
// (which draws the cells) and host-side tests (which verify hit-test).

#include <stdint.h>

#define DSKY_KP_X0       64
#define DSKY_KP_X1      320
#define DSKY_KP_Y0      100
#define DSKY_KP_Y1      240
#define DSKY_KP_COLS      6
#define DSKY_KP_ROWS      5
#define DSKY_KP_CW       42
#define DSKY_KP_CH       28

typedef struct { int col, row; int code; const char *label; } dsky_kp_cell_t;
extern const dsky_kp_cell_t dsky_kp_cells_320x240[];
extern const int            dsky_kp_cells_320x240_count;

// Returns dsky_key_t (0..31) or -1 if outside any cell.
int dsky_keypad_320x240_hit(int x, int y);
