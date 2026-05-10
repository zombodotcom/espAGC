// test_render_prog_lit — region assertion. Render a state with PROG=12,
// VERB=16, NOUN=65 and confirm the renderer paints amber pixels in the
// PROG digit region. Robust to small font/spacing tweaks; only fires if
// the digits aren't being painted at all (e.g. row-mapping regression).

#include "test_helpers.h"
#include "dsky_layout.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define W 320
#define H 240
#define COL_AMBER 0xFD20

int main(void)
{
    const dsky_layout_t *L = dsky_layout_for(W, H);
    ASSERT(L != NULL, "no layout for %dx%d", W, H);

    static uint16_t fb[W * H];
    dsky_state_t s = { 0 };
    for (int i = 0; i < 5; i++) {
        s.r1[i] = s.r2[i] = s.r3[i] = DSKY_BLANK;
    }
    s.prog[0] = 1; s.prog[1] = 2;
    s.verb[0] = 1; s.verb[1] = 6;
    s.noun[0] = 6; s.noun[1] = 5;

    for (int y0 = 0; y0 < H; y0 += L->strip_h) {
        L->render_strip(&fb[y0 * W], &s, y0);
    }

    // PROG digits live at roughly x=296..308, y=4..12 per the renderer
    // (RW_X0=64 + 232, y=4, FONT_H=7). Open the box up a bit for slop.
    int amber = 0;
    for (int y = 0; y < 16; y++) {
        for (int x = 290; x < 320; x++) {
            if (fb[y * W + x] == COL_AMBER) amber++;
        }
    }
    printf("amber pixels in PROG region: %d\n", amber);
    ASSERT(amber >= 8, "PROG=12 produced too few amber pixels (%d) — "
           "row-11 decoder probably regressed", amber);

    // Also make sure the NOUN/VERB regions have amber (rows 9 and 10 of
    // ch10). Same shape; just shift x.
    int amber_verb = 0, amber_noun = 0;
    for (int y = 18; y < 36; y++) {
        for (int x = 78;  x < 130; x++)  if (fb[y * W + x] == COL_AMBER) amber_verb++;
        for (int x = 194; x < 246; x++)  if (fb[y * W + x] == COL_AMBER) amber_noun++;
    }
    printf("amber pixels: VERB=%d NOUN=%d\n", amber_verb, amber_noun);
    ASSERT(amber_verb >= 8, "VERB digits not painted");
    ASSERT(amber_noun >= 8, "NOUN digits not painted");

    PASS();
}
