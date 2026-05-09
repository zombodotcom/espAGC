#pragma once
#include <stdint.h>

#define FONT_W           5
#define FONT_H           7
#define FONT_GLYPH_BYTES 5

#define FONT_SPACE     0
#define FONT_PLUS      1
#define FONT_MINUS     2
#define FONT_DIGIT_0   3        // 3..12
#define FONT_LETTER_A  13       // 13..38
#define FONT_GLYPH_COUNT (FONT_LETTER_A + 26)

extern const uint8_t font5x7[FONT_GLYPH_COUNT][FONT_GLYPH_BYTES];
int font_index(char c);
