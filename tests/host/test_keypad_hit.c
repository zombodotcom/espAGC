// tests/host/test_keypad_hit.c
//
// Layer-1 unit test for the 320x240 DSKY keypad hit-test. Verifies a tap
// at the visual center of each cell maps to the right dsky_key_t.

#include <stdio.h>
#include <assert.h>
#include "dsky_keypad_320x240.h"
#include "dsky_keys.h"

#define X_OF(col)  (DSKY_KP_X0 + (col) * DSKY_KP_CW + DSKY_KP_CW / 2)
#define Y_OF(row)  (DSKY_KP_Y0 + (row) * DSKY_KP_CH + DSKY_KP_CH / 2)

int main(void)
{
    // Layout (col x row):
    //   row 0:  V  +  7  8  9  C
    //   row 1:  N  -  4  5  6  P
    //   row 2:  -  -  1  2  3  K
    //   row 3:  -  0  -  -  -  E
    //   row 4:  -  -  -  -  -  R

    assert(dsky_keypad_320x240_hit(X_OF(0), Y_OF(0)) == DSKY_KEY_VERB);
    assert(dsky_keypad_320x240_hit(X_OF(1), Y_OF(0)) == DSKY_KEY_PLUS);
    assert(dsky_keypad_320x240_hit(X_OF(2), Y_OF(0)) == DSKY_KEY_7);
    assert(dsky_keypad_320x240_hit(X_OF(5), Y_OF(0)) == DSKY_KEY_CLR);

    assert(dsky_keypad_320x240_hit(X_OF(0), Y_OF(1)) == DSKY_KEY_NOUN);
    assert(dsky_keypad_320x240_hit(X_OF(1), Y_OF(1)) == DSKY_KEY_MINUS);
    assert(dsky_keypad_320x240_hit(X_OF(5), Y_OF(1)) == DSKY_KEY_PRO);

    assert(dsky_keypad_320x240_hit(X_OF(2), Y_OF(2)) == DSKY_KEY_1);
    assert(dsky_keypad_320x240_hit(X_OF(5), Y_OF(2)) == DSKY_KEY_KEYREL);

    assert(dsky_keypad_320x240_hit(X_OF(1), Y_OF(3)) == DSKY_KEY_0);
    assert(dsky_keypad_320x240_hit(X_OF(5), Y_OF(3)) == DSKY_KEY_ENTR);

    assert(dsky_keypad_320x240_hit(X_OF(5), Y_OF(4)) == DSKY_KEY_RSET);

    // Out-of-bounds returns -1.
    assert(dsky_keypad_320x240_hit(0, 0) == -1);
    assert(dsky_keypad_320x240_hit(63, 100) == -1);
    assert(dsky_keypad_320x240_hit(64, 99) == -1);
    assert(dsky_keypad_320x240_hit(320, 240) == -1);

    printf("test_keypad_hit OK\n");
    return 0;
}
