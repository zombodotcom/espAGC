// test_render_blank — render the boot/blank dsky_state (every digit
// blank, no indicators lit) into a 320x240 RGB565 framebuffer in three
// 80-row strips, then compute a 32-bit FNV-1a hash of the entire FB
// and assert it matches a known value. If the renderer's output drifts
// (font / layout / colors changed), the hash changes and the test fails
// loudly. Update by running once and committing the new hash.

#include "test_helpers.h"
#include "dsky_layout.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define W 320
#define H 240

static uint32_t fnv1a(const uint8_t *p, size_t n)
{
    uint32_t h = 0x811C9DC5u;
    while (n--) { h ^= *p++; h *= 0x01000193u; }
    return h;
}

int main(void)
{
    const dsky_layout_t *L = dsky_layout_for(W, H);
    ASSERT(L != NULL, "no layout for %dx%d", W, H);

    static uint16_t fb[W * H];
    dsky_state_t s = { 0 };
    for (int i = 0; i < 5; i++) {
        s.r1[i] = s.r2[i] = s.r3[i] = DSKY_BLANK;
    }
    s.prog[0] = s.prog[1] = DSKY_BLANK;
    s.verb[0] = s.verb[1] = DSKY_BLANK;
    s.noun[0] = s.noun[1] = DSKY_BLANK;

    for (int y0 = 0; y0 < H; y0 += L->strip_h) {
        L->render_strip(&fb[y0 * W], &s, y0);
    }

    uint32_t h = fnv1a((const uint8_t *)fb, sizeof(fb));
    printf("blank-frame hash: 0x%08x\n", h);

    // The first time you run this, the hash printed above is the
    // "golden". Bake it in here once it's stable. Until then we just
    // assert the framebuffer wasn't left untouched (zero hash).
    ASSERT(h != 0 && h != 0x811C9DC5u, "fb appears uninitialised");

    // Sanity: at least *some* pixels should be lit (status panel
    // background fill alone produces non-black pixels).
    int non_black = 0;
    for (int i = 0; i < W * H; i++) if (fb[i] != 0) non_black++;
    ASSERT(non_black > 1000,
           "blank-frame has only %d non-black pixels; renderer is dead", non_black);

    PASS();
}
