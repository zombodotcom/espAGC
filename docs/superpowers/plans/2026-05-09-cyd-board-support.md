# CYD Board Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the ESP32-2432S028C ("Cheap Yellow Display" / CYD, 2.8" capacitive) as a second standalone build target alongside the existing LilyGO T-Dongle-C5, sharing one codebase. Selected at build time via `idf.py set-target`.

**Architecture:** Promote `display_hal`'s implicit ST7735+APA102 dependency into three thin C interfaces (`display_panel_iface_t`, `panel_touch_iface_t`, `led_status_iface_t`) and a resolution-keyed renderer (`dsky_layout_t`). Each board component returns concrete impls via `board_get_panel/touch/led`. Top-level CMake selects the board component from `IDF_TARGET`. Phase 0 refactors T-Dongle behind these interfaces with zero behavior change; phases 1-6 layer CYD support on top.

**Tech Stack:** ESP-IDF v6.0+ (esp32 + esp32c5), C11, FreeRTOS, ILI9341 + CST820 (CYD), ST7735 + APA102 (T-Dongle), direct-to-framebuffer rendering (no LVGL).

**Spec:** `docs/superpowers/specs/2026-05-09-cyd-board-support-design.md`

---

## Phase 0 — Refactor T-Dongle behind ifaces (no behavior change)

The existing T-Dongle build must keep booting, displaying, and accepting input identically after every task in this phase. Verification at each step is "T-Dongle build succeeds and on-device behavior is unchanged."

### Task 1: Add display_panel_iface.h

**Files:**
- Create: `components/display_hal/include/display_panel_iface.h`

- [ ] **Step 1: Create the iface header**

```c
// components/display_hal/include/display_panel_iface.h
#pragma once
//
// display_panel_iface_t — board-agnostic LCD interface used by display_hal.
// Each board component returns a pointer to a static instance via
// board_get_panel(). The renderer pushes RGB565 pixel strips through
// draw_rows; the panel impl handles byte order and address-window setup.

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int       width;          // pixels (panel coords, post-rotation)
    int       height;
    bool      swap_bytes;     // (informational) true if impl byte-swaps
    esp_err_t (*init)(void);
    // Push a strip of rows [y0, y1) of an RGB565 buffer (host byte order).
    // Caller guarantees the buffer is `width * (y1 - y0)` u16 pixels.
    esp_err_t (*draw_rows)(int y0, int y1, const uint16_t *pixels);
} display_panel_iface_t;

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Verify T-Dongle build still succeeds**

Run: `idf.py build`
Expected: build succeeds (header is unused so far).

- [ ] **Step 3: Commit**

```bash
git add components/display_hal/include/display_panel_iface.h
git commit -m "display_hal: add display_panel_iface_t header"
```

---

### Task 2: Add panel_touch_iface.h and led_status_iface.h

**Files:**
- Create: `components/touch_input/include/panel_touch_iface.h`
- Create: `components/led_status/include/led_status_iface.h`

- [ ] **Step 1: Create the touch iface (just the header — component dir comes later)**

Create `components/touch_input/` directory by creating the header file inside `components/touch_input/include/`.

```c
// components/touch_input/include/panel_touch_iface.h
#pragma once
//
// panel_touch_iface_t — board-agnostic touchscreen interface. Boards
// without a touchscreen (e.g. T-Dongle) return NULL from board_get_touch().

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_err_t (*init)(void);
    // Returns true if currently pressed. (*x,*y) in panel coords (post-rotation).
    bool (*poll)(int *x, int *y);
} panel_touch_iface_t;

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Create the LED status iface header**

```c
// components/led_status/include/led_status_iface.h
#pragma once
//
// led_status_iface_t — board-agnostic single-RGB-LED interface.
// Boards without an LED return NULL from board_get_led().

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*init)(void);
    void (*set_rgb)(uint8_t r, uint8_t g, uint8_t b);
} led_status_iface_t;

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 3: Verify T-Dongle build still succeeds**

Run: `idf.py build`
Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
git add components/touch_input/include/panel_touch_iface.h components/led_status/include/led_status_iface.h
git commit -m "ifaces: add panel_touch_iface_t and led_status_iface_t headers"
```

---

### Task 3: Add dsky_layout.h (renderer iface)

**Files:**
- Create: `components/display_hal/include/dsky_layout.h`

- [ ] **Step 1: Create the dsky_layout header**

```c
// components/display_hal/include/dsky_layout.h
#pragma once
//
// dsky_layout_t — resolution-keyed DSKY renderer. display_hal looks one up
// via dsky_layout_for(panel_w, panel_h) and renders the framebuffer in
// strip_h-row passes. New panel resolutions add a layout instance and
// register it in dsky_layout.c's table.

#include "dsky_state.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int  fb_w;
    int  fb_h;
    int  strip_h;             // divides fb_h evenly. == fb_h means full-frame.
    void (*init_strip)(uint16_t *strip, int y0);   // splash; called once per strip at boot
    void (*render_strip)(uint16_t *strip, const dsky_state_t *s, int y0);
    int  (*hit_test)(int x, int y);                // -1 if outside any button; NULL = no touch
} dsky_layout_t;

const dsky_layout_t *dsky_layout_for(int w, int h);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Verify T-Dongle build still succeeds**

Run: `idf.py build`
Expected: build succeeds.

- [ ] **Step 3: Commit**

```bash
git add components/display_hal/include/dsky_layout.h
git commit -m "display_hal: add dsky_layout_t renderer iface"
```

---

### Task 4: Lift the existing 160x80 renderer into dsky_render_160x80.c

**Files:**
- Create: `components/display_hal/dsky_render_160x80.c`
- Modify: `components/display_hal/CMakeLists.txt`
- Modify: `components/display_hal/display_hal.c` (logic moved out, will be stripped down in Task 8)

- [ ] **Step 1: Create dsky_render_160x80.c**

```c
// components/display_hal/dsky_render_160x80.c
//
// 160x80 DSKY layout — the original T-Dongle text renderer. Single full-frame
// strip (strip_h == fb_h == 80). No hit-test (T-Dongle has no touch).

#include "dsky_layout.h"
#include "font5x7.h"

#include <stdio.h>
#include <string.h>

#define FB_W  160
#define FB_H   80

#define COL_BG    0x0000
#define COL_AMBER 0xFD20
#define COL_DIM   0x4208
#define COL_RED   0xF800
#define COL_GREEN 0x07E0

static void put_pixel(uint16_t *fb, int x, int y, uint16_t c)
{
    if ((unsigned)x < FB_W && (unsigned)y < FB_H) fb[y * FB_W + x] = c;
}

static void draw_glyph(uint16_t *fb, int x0, int y0, int idx, uint16_t c)
{
    const uint8_t *g = font5x7[idx];
    for (int col = 0; col < FONT_W; col++) {
        uint8_t bits = g[col];
        for (int row = 0; row < FONT_H; row++) {
            if (bits & (1 << row)) put_pixel(fb, x0 + col, y0 + row, c);
        }
    }
}

static void draw_text(uint16_t *fb, int x0, int y0, const char *s, uint16_t c)
{
    int x = x0;
    for (; *s; s++) {
        draw_glyph(fb, x, y0, font_index(*s), c);
        x += FONT_W + 1;
    }
}

static char digit_char(int8_t d) { return d < 0 ? ' ' : (char)('0' + d); }
static char sign_char (int s)    {
    return s == DSKY_SIGN_PLUS ? '+' : s == DSKY_SIGN_MINUS ? '-' : ' ';
}

static void init_strip(uint16_t *fb, int y0)
{
    (void)y0;
    memset(fb, 0, FB_W * FB_H * sizeof(uint16_t));
    draw_text(fb, 2,  2, "ESPAGC",  COL_AMBER);
    draw_text(fb, 2, 12, "BOOTING", COL_DIM);
}

static void render_strip(uint16_t *fb, const dsky_state_t *s, int y0)
{
    (void)y0;
    memset(fb, 0, FB_W * FB_H * sizeof(uint16_t));

    char line[32];
    draw_text(fb, 2, 0, "ESPAGC", COL_AMBER);

    snprintf(line, sizeof line, "PRG %c%c  VRB %c%c",
             digit_char(s->prog[0]), digit_char(s->prog[1]),
             digit_char(s->verb[0]), digit_char(s->verb[1]));
    draw_text(fb, 2, 10, line, COL_AMBER);

    snprintf(line, sizeof line, "NUN %c%c", digit_char(s->noun[0]), digit_char(s->noun[1]));
    draw_text(fb, 2, 20, line, COL_AMBER);

    snprintf(line, sizeof line, "R1 %c%c%c%c%c%c",
             sign_char(s->r1_sign),
             digit_char(s->r1[0]), digit_char(s->r1[1]),
             digit_char(s->r1[2]), digit_char(s->r1[3]), digit_char(s->r1[4]));
    draw_text(fb, 2, 32, line, COL_AMBER);

    snprintf(line, sizeof line, "R2 %c%c%c%c%c%c",
             sign_char(s->r2_sign),
             digit_char(s->r2[0]), digit_char(s->r2[1]),
             digit_char(s->r2[2]), digit_char(s->r2[3]), digit_char(s->r2[4]));
    draw_text(fb, 2, 42, line, COL_AMBER);

    snprintf(line, sizeof line, "R3 %c%c%c%c%c%c",
             sign_char(s->r3_sign),
             digit_char(s->r3[0]), digit_char(s->r3[1]),
             digit_char(s->r3[2]), digit_char(s->r3[3]), digit_char(s->r3[4]));
    draw_text(fb, 2, 52, line, COL_AMBER);

    int x = 2;
    if (s->comp_acty)   { draw_text(fb, x, 64, "CA",  COL_GREEN); x += 18; }
    if (s->uplink_acty) { draw_text(fb, x, 64, "UP",  COL_GREEN); x += 18; }
    if (s->prog_alarm)  { draw_text(fb, x, 64, "PA",  COL_RED);   x += 18; }
    if (s->opr_err)     { draw_text(fb, x, 64, "OE",  COL_RED);   x += 18; }
    if (s->stby)        { draw_text(fb, x, 64, "SBY", COL_DIM);   x += 24; }
}

const dsky_layout_t dsky_layout_160x80 = {
    .fb_w         = FB_W,
    .fb_h         = FB_H,
    .strip_h      = FB_H,            // single full-frame pass
    .init_strip   = init_strip,
    .render_strip = render_strip,
    .hit_test     = NULL,            // T-Dongle: no touchscreen
};
```

- [ ] **Step 2: Add file to display_hal CMakeLists**

Modify `components/display_hal/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS         "display_hal.c" "st7735_panel.c" "font5x7.c"
                 "dsky_render_160x80.c"
    INCLUDE_DIRS "include"
    REQUIRES     channel_router log esp_driver_gpio esp_driver_spi esp_psram
)
```

- [ ] **Step 3: Verify T-Dongle build still succeeds**

Run: `idf.py build`
Expected: build succeeds. (Layout is defined but not yet referenced — should compile clean.)

- [ ] **Step 4: Commit**

```bash
git add components/display_hal/dsky_render_160x80.c components/display_hal/CMakeLists.txt
git commit -m "display_hal: lift 160x80 renderer into dsky_render_160x80.c"
```

---

### Task 5: Add dsky_layout_for() registry

**Files:**
- Create: `components/display_hal/dsky_layout.c`
- Modify: `components/display_hal/CMakeLists.txt`

- [ ] **Step 1: Create the registry**

```c
// components/display_hal/dsky_layout.c
//
// Resolution → renderer lookup. Add new layouts here.

#include "dsky_layout.h"
#include <stddef.h>

extern const dsky_layout_t dsky_layout_160x80;

const dsky_layout_t *dsky_layout_for(int w, int h)
{
    if (w == 160 && h == 80) return &dsky_layout_160x80;
    return NULL;
}
```

- [ ] **Step 2: Add file to CMakeLists**

Modify `components/display_hal/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS         "display_hal.c" "st7735_panel.c" "font5x7.c"
                 "dsky_render_160x80.c" "dsky_layout.c"
    INCLUDE_DIRS "include"
    REQUIRES     channel_router log esp_driver_gpio esp_driver_spi esp_psram
)
```

- [ ] **Step 3: Verify T-Dongle build still succeeds**

Run: `idf.py build`
Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
git add components/display_hal/dsky_layout.c components/display_hal/CMakeLists.txt
git commit -m "display_hal: dsky_layout_for() registry"
```

---

### Task 6: Repackage st7735 behind display_panel_iface_t

**Files:**
- Create: `components/display_hal/panel_st7735.c`
- Modify: `components/display_hal/CMakeLists.txt`

Leave `st7735_panel.c` untouched — including the per-frame `apa102_off_frame()` call inside `st7735_draw_rows()`, which is load-bearing (the file's own header comment notes that the LED latches noise from neighboring traces without it). Just add a thin iface adapter file alongside.

- [ ] **Step 1: Create panel_st7735.c iface wrapper**

```c
// components/display_hal/panel_st7735.c
//
// display_panel_iface_t adapter for the existing ST7735 driver.
// The underlying driver is st7735_panel.c (keeps the proven init sequence
// for the T-Dongle's panel; see file header for the long story on why we
// don't use the maintained esp_lcd_st7735 component).

#include "display_panel_iface.h"
#include "st7735_panel.h"

static esp_err_t panel_init(void) { return st7735_init(); }
static esp_err_t panel_draw_rows(int y0, int y1, const uint16_t *pixels)
{
    return st7735_draw_rows(y0, y1, pixels);
}

const display_panel_iface_t display_panel_st7735_t_dongle = {
    .width      = DSKY_FB_W,        // 160
    .height     = DSKY_FB_H,        //  80
    .swap_bytes = true,             // st7735_draw_rows already byte-swaps
    .init       = panel_init,
    .draw_rows  = panel_draw_rows,
};
```

- [ ] **Step 2: Add file to CMakeLists**

Modify `components/display_hal/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS         "display_hal.c" "st7735_panel.c" "panel_st7735.c"
                 "font5x7.c" "dsky_render_160x80.c" "dsky_layout.c"
    INCLUDE_DIRS "include"
    REQUIRES     channel_router log esp_driver_gpio esp_driver_spi esp_psram
)
```

- [ ] **Step 3: Verify T-Dongle build still succeeds**

Run: `idf.py build`
Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
git add components/display_hal/panel_st7735.c components/display_hal/CMakeLists.txt
git commit -m "display_hal: ST7735 behind display_panel_iface_t"
```

---

### Task 7: Repackage APA102 silence + future-LED behind led_status_iface_t

**Files:**
- Modify: `components/led_status/CMakeLists.txt`
- Create: `components/led_status/apa102_iface.c`
- Modify: `components/led_status/apa102_status.c` (expose silence helper)

The existing `apa102_status.c` is currently unused (commented out in `app_main.c`). We keep the silence-on-boot behavior (so the LED doesn't flicker), wrap it as the T-Dongle's `led_status_iface_t` impl, but leave `set_rgb` as a working APA102 driver for when display ownership of GPIO 4/5 gets cleaner.

- [ ] **Step 1: Read current apa102_status.c to confirm what it exports**

Run: `cat components/led_status/apa102_status.c | head -40`
Expected output: a single-LED APA102 driver with `led_status_init()` and `led_status_set_rgb()` style functions.

- [ ] **Step 2: Create the iface adapter**

```c
// components/led_status/apa102_iface.c
//
// led_status_iface_t adapter for the T-Dongle's onboard APA102 RGB LED.
// (Pin 4=CLK, 5=DATA — see board_pins.h.) The actual bit-banged driver
// lives in apa102_status.c; this file just wraps it as an iface instance.

#include "led_status_iface.h"
#include "led_status.h"

static void iface_init(void)                              { led_status_init(); }
static void iface_set_rgb(uint8_t r, uint8_t g, uint8_t b){ led_status_set_rgb(r, g, b); }

const led_status_iface_t led_status_apa102_t_dongle = {
    .init    = iface_init,
    .set_rgb = iface_set_rgb,
};
```

- [ ] **Step 3: Update CMakeLists to include the adapter and the iface include path**

Modify `components/led_status/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS         "apa102_status.c" "apa102_iface.c"
    INCLUDE_DIRS "include"
    REQUIRES     log esp_driver_gpio
)
```

- [ ] **Step 4: Verify T-Dongle build still succeeds**

Run: `idf.py build`
Expected: build succeeds.

- [ ] **Step 5: Commit**

```bash
git add components/led_status/apa102_iface.c components/led_status/CMakeLists.txt
git commit -m "led_status: APA102 behind led_status_iface_t"
```

---

### Task 8: Make T-Dongle board component a factory

**Files:**
- Modify: `components/board_tdongle_c5/board_init.c`
- Modify: `components/board_tdongle_c5/include/board_pins.h`
- Modify: `components/board_tdongle_c5/CMakeLists.txt`

- [ ] **Step 1: Add factory declarations to board_pins.h**

Modify `components/board_tdongle_c5/include/board_pins.h` — append before the closing `#ifdef __cplusplus` block at the bottom:

```c
#include "display_panel_iface.h"
#include "panel_touch_iface.h"
#include "led_status_iface.h"

const display_panel_iface_t *board_get_panel(void);
const panel_touch_iface_t   *board_get_touch(void);   // NULL on T-Dongle
const led_status_iface_t    *board_get_led(void);
```

- [ ] **Step 2: Implement the factories in board_init.c**

Replace `components/board_tdongle_c5/board_init.c` with:

```c
#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "board";

extern const display_panel_iface_t display_panel_st7735_t_dongle;
extern const led_status_iface_t    led_status_apa102_t_dongle;

const display_panel_iface_t *board_get_panel(void) { return &display_panel_st7735_t_dongle; }
const panel_touch_iface_t   *board_get_touch(void) { return NULL; }
const led_status_iface_t    *board_get_led(void)   { return &led_status_apa102_t_dongle; }

void board_init(void)
{
    gpio_config_t btn = {
        .pin_bit_mask = 1ULL << BOARD_BUTTON_BOOT,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn);

    ESP_LOGI(TAG, "%s board init complete", BOARD_NAME);
}
```

- [ ] **Step 3: Add REQUIRES on display_hal and led_status so factory headers resolve**

Modify `components/board_tdongle_c5/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS         "board_init.c"
    INCLUDE_DIRS "include"
    REQUIRES     log esp_driver_gpio display_hal touch_input led_status
)
```

- [ ] **Step 4: Make touch_input a real component (placeholder CMakeLists)**

The `touch_input` component currently has just a header. Add a CMakeLists so it's a real ESP-IDF component:

Create `components/touch_input/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS         ""
    INCLUDE_DIRS "include"
)
```

(Empty SRCS is legal — IDF treats it as a header-only component for now. Files get added in Phase 4.)

- [ ] **Step 5: Verify T-Dongle build still succeeds**

Run: `idf.py build`
Expected: build succeeds.

- [ ] **Step 6: Commit**

```bash
git add components/board_tdongle_c5/ components/touch_input/CMakeLists.txt
git commit -m "board_tdongle_c5: implement board_get_panel/touch/led factories"
```

---

### Task 9: Refactor display_hal.c to drive panels through the iface + dsky_layout

**Files:**
- Modify: `components/display_hal/display_hal.c`
- Modify: `components/display_hal/CMakeLists.txt`

- [ ] **Step 1: Replace display_hal.c contents**

```c
// components/display_hal/display_hal.c
//
// Driver-agnostic glue. Looks up the panel iface (from the board) and the
// matching DSKY layout (from resolution), allocates a single strip-sized
// scratch buffer, and pushes frames in `strip_h`-row passes.
//
// On 160x80 this is a single full-frame pass (strip_h == fb_h == 80) — the
// existing behavior on T-Dongle. On 320x240 (CYD) it's three passes per
// frame at the 30Hz UI tick.

#include "display_hal.h"
#include "display_panel_iface.h"
#include "dsky_layout.h"

#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

// Provided by the active board component.
extern const display_panel_iface_t *board_get_panel(void);

static const char *TAG = "dsky";

static const display_panel_iface_t *s_panel;
static const dsky_layout_t         *s_layout;
static uint16_t                    *s_strip;        // fb_w * strip_h pixels

void display_hal_init(void)
{
    s_panel = board_get_panel();
    if (!s_panel) {
        ESP_LOGE(TAG, "board returned no panel");
        return;
    }
    s_layout = dsky_layout_for(s_panel->width, s_panel->height);
    if (!s_layout) {
        ESP_LOGE(TAG, "no DSKY layout for %dx%d", s_panel->width, s_panel->height);
        return;
    }

    size_t bytes = s_panel->width * s_layout->strip_h * sizeof(uint16_t);
    s_strip = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_strip) s_strip = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    if (!s_strip) { ESP_LOGE(TAG, "strip alloc (%u B) failed", (unsigned)bytes); return; }

    if (s_panel->init() != ESP_OK) { ESP_LOGE(TAG, "panel init failed"); return; }

    // Splash: walk strips, init each, push.
    for (int y0 = 0; y0 < s_layout->fb_h; y0 += s_layout->strip_h) {
        s_layout->init_strip(s_strip, y0);
        s_panel->draw_rows(y0, y0 + s_layout->strip_h, s_strip);
    }

    ESP_LOGI(TAG, "display_hal up: %dx%d, strip_h=%d",
             s_panel->width, s_panel->height, s_layout->strip_h);
}

void display_hal_update(const dsky_state_t *s)
{
    if (!s_strip || !s_panel || !s_layout) return;

    for (int y0 = 0; y0 < s_layout->fb_h; y0 += s_layout->strip_h) {
        s_layout->render_strip(s_strip, s, y0);
        s_panel->draw_rows(y0, y0 + s_layout->strip_h, s_strip);
    }
}
```

- [ ] **Step 2: Verify T-Dongle build succeeds**

Run: `idf.py build`
Expected: build succeeds.

- [ ] **Step 3: Flash to T-Dongle and verify behavior is unchanged**

Run: `idf.py -p COM<n> flash monitor`
Expected on-device:
- Boot log shows `display_hal up: 160x80, strip_h=80`.
- Splash shows "ESPAGC / BOOTING".
- After ROM load, registers update normally.
- WiFi/web DSKY still works.
- LED is silent (no flicker).

If flashing isn't possible right now, skip the on-device step but flag in commit message that it's untested.

- [ ] **Step 4: Commit**

```bash
git add components/display_hal/display_hal.c
git commit -m "display_hal: drive panel + layout through ifaces"
```

---

## Phase 1 — CYD board scaffolding (build target exists, no panel yet)

After Phase 1, `idf.py -B build_cyd -D SDKCONFIG=sdkconfig.cyd set-target esp32 build` succeeds and produces a binary that boots, runs the AGC engine, exposes WiFi, but has a blank screen (no panel impl yet).

### Task 10: Add witnessmenow CYD reference as submodule

**Files:**
- Modify: `.gitmodules`
- Add: `third_party/CYD-reference/`

- [ ] **Step 1: Add the submodule**

Run:
```powershell
git submodule add https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display.git third_party/CYD-reference
```

Expected: `.gitmodules` gains a new entry; `third_party/CYD-reference/` is populated.

- [ ] **Step 2: Verify CYD-reference contains the C-variant pinout**

Run:
```powershell
ls third_party/CYD-reference
```
Expected: a `README.md` and a `Docs/` (or similar) directory with pin maps. (Used as documentation only; nothing in this repo compiles from it.)

- [ ] **Step 3: Commit**

```bash
git add .gitmodules third_party/CYD-reference
git commit -m "third_party: add witnessmenow/ESP32-Cheap-Yellow-Display as submodule"
```

---

### Task 11: Split sdkconfig.defaults into common + per-target files

**Files:**
- Modify: `sdkconfig.defaults`
- Create: `sdkconfig.defaults.esp32c5`
- Create: `sdkconfig.defaults.esp32`
- Create: `partitions_cyd.csv`

- [ ] **Step 1: Move the C5-specific lines out of sdkconfig.defaults**

Replace `sdkconfig.defaults` with:

```
# Common to all targets. Per-target knobs live in sdkconfig.defaults.<target>.

# Custom partition table — filename is target-specific (see per-target file).
CONFIG_PARTITION_TABLE_CUSTOM=y

# Larger main task stack (LVGL + glue, even though LVGL is not used yet)
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192

# Logging
CONFIG_LOG_DEFAULT_LEVEL_INFO=y

# Compiler
CONFIG_COMPILER_OPTIMIZATION_PERF=y

# WiFi
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=8
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=16
```

- [ ] **Step 2: Create sdkconfig.defaults.esp32c5 (T-Dongle, the existing config)**

```
CONFIG_IDF_TARGET="esp32c5"

# Flash and PSRAM (T-Dongle-C5: 16 MB QIO flash, 8 MB QIO PSRAM)
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_QUAD=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_USE_MALLOC=y

CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
```

- [ ] **Step 3: Create sdkconfig.defaults.esp32 (CYD)**

```
CONFIG_IDF_TARGET="esp32"

# Flash (CYD-2432S028C: 4 MB DIO flash, no PSRAM on the standard variant)
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_ESPTOOLPY_FLASHMODE_DIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_40M=y

CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_cyd.csv"

# Default touch driver for CYD — capacitive 2.8" 2432S028C
CONFIG_ESPAGC_CYD_TOUCH_CST820=y
```

- [ ] **Step 4: Create partitions_cyd.csv**

Read the existing `partitions.csv` first to understand its layout:

Run: `cat partitions.csv`

Then create `partitions_cyd.csv` based on it, scaled to 4 MB total flash. A typical layout:

```
# Name,   Type, SubType, Offset,  Size,    Flags
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000, 0x3F0000,
```

(Adjust to match the partition layout actually used in `partitions.csv` — e.g., if the existing file has a `storage` partition, scale it down proportionally to fit 4 MB.)

- [ ] **Step 5: Verify T-Dongle build still succeeds**

Run: `idf.py fullclean && idf.py set-target esp32c5 && idf.py build`
Expected: build succeeds. Boot log on flash should still be unchanged.

- [ ] **Step 6: Commit**

```bash
git add sdkconfig.defaults sdkconfig.defaults.esp32c5 sdkconfig.defaults.esp32 partitions_cyd.csv
git commit -m "sdkconfig: split common / per-target defaults; add CYD partition table"
```

---

### Task 12: Update .gitignore for sdkconfig.cyd

**Files:**
- Modify: `.gitignore`

- [ ] **Step 1: Add sdkconfig.cyd to .gitignore**

Modify `.gitignore`. Replace the line `sdkconfig` with:

```
sdkconfig
sdkconfig.cyd
sdkconfig.old
```

- [ ] **Step 2: Commit**

```bash
git add .gitignore
git commit -m ".gitignore: ignore sdkconfig.cyd (CYD per-board sdkconfig)"
```

---

### Task 13: Create board_cyd_2432s028 component skeleton

**Files:**
- Create: `components/board_cyd_2432s028/CMakeLists.txt`
- Create: `components/board_cyd_2432s028/include/board_pins.h`
- Create: `components/board_cyd_2432s028/board_init.c`

- [ ] **Step 1: Create board_pins.h**

```c
// components/board_cyd_2432s028/include/board_pins.h
#pragma once
//
// Pin map for ESP32-2432S028C ("Cheap Yellow Display", 2.8" capacitive variant).
// Reference: third_party/CYD-reference (witnessmenow/ESP32-Cheap-Yellow-Display).
//
// The "C" capacitive variant moves the LCD backlight from GPIO21 (where the
// "R" resistive variant has it) to GPIO27 specifically to free GPIO21 for
// the CST820 touch INT line.

#define BOARD_NAME "CYD-2432S028C"

// LCD: ILI9341, 240x320 native; rotated to 320x240 landscape via MADCTL.
#define BOARD_LCD_HRES   320
#define BOARD_LCD_VRES   240
#define BOARD_LCD_SCK    14
#define BOARD_LCD_MOSI   13
#define BOARD_LCD_MISO   12      // shared with onboard SD slot
#define BOARD_LCD_CS     15
#define BOARD_LCD_DC      2
#define BOARD_LCD_RST    -1      // tied to EN on PCB; no GPIO control
#define BOARD_LCD_BL     27      // PWM-able active-high backlight

// Touch: CST820 capacitive (I2C, 7-bit addr 0x15).
#define BOARD_TOUCH_SDA  33
#define BOARD_TOUCH_SCL  32
#define BOARD_TOUCH_RST  25
#define BOARD_TOUCH_INT  21

// RGB LED — 3 separate active-low GPIOs.
#define BOARD_LED_R       4
#define BOARD_LED_G      16
#define BOARD_LED_B      17

// SD card (unused).
#define BOARD_SD_CS       5

// Boot button (pulled high; pressed = low) — standard ESP32 BOOT pin.
#define BOARD_BUTTON_BOOT 0

#ifdef __cplusplus
extern "C" {
#endif

#include "display_panel_iface.h"
#include "panel_touch_iface.h"
#include "led_status_iface.h"

void board_init(void);
const display_panel_iface_t *board_get_panel(void);
const panel_touch_iface_t   *board_get_touch(void);
const led_status_iface_t    *board_get_led(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Create stub board_init.c (panels not implemented yet — return NULL)**

```c
// components/board_cyd_2432s028/board_init.c
#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "board";

const display_panel_iface_t *board_get_panel(void) { return NULL; }   // TODO Task 15
const panel_touch_iface_t   *board_get_touch(void) { return NULL; }   // TODO Task 19
const led_status_iface_t    *board_get_led(void)   { return NULL; }   // TODO Task 22

void board_init(void)
{
    gpio_config_t btn = {
        .pin_bit_mask = 1ULL << BOARD_BUTTON_BOOT,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn);

    ESP_LOGI(TAG, "%s board init complete", BOARD_NAME);
}
```

(The `TODO` comments here are fine — they reference exact tasks in this plan that fill them in. They're not placeholder requirements; they're a deliberate phase-staging marker that gets removed in Tasks 15/19/22.)

- [ ] **Step 3: Create CMakeLists.txt**

```cmake
# components/board_cyd_2432s028/CMakeLists.txt
idf_component_register(
    SRCS         "board_init.c"
    INCLUDE_DIRS "include"
    REQUIRES     log esp_driver_gpio display_hal touch_input led_status
)
```

- [ ] **Step 4: Commit**

```bash
git add components/board_cyd_2432s028/
git commit -m "board_cyd_2432s028: skeleton with pin map + stub factories"
```

---

### Task 14: Top-level CMakeLists.txt selects board from IDF_TARGET

**Files:**
- Modify: `CMakeLists.txt`
- Move: `components/board_tdongle_c5/` → `boards/board_tdongle_c5/`
- Move: `components/board_cyd_2432s028/` → `boards/board_cyd_2432s028/`

Both board components must NOT live under `components/`. ESP-IDF auto-discovers every subdirectory of an `EXTRA_COMPONENT_DIRS` entry, so if both boards sat in `components/` they'd both compile and produce duplicate `board_get_panel` symbols. Move them to a sibling `boards/` directory and only point `EXTRA_COMPONENT_DIRS` at the *selected* one.

- [ ] **Step 1: Move both board component dirs to boards/**

Run:
```powershell
git mv components/board_tdongle_c5 boards/board_tdongle_c5
git mv components/board_cyd_2432s028 boards/board_cyd_2432s028
```

- [ ] **Step 2: Replace CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.16)

if(IDF_TARGET STREQUAL "esp32c5")
    set(BOARD_COMPONENT board_tdongle_c5)
elseif(IDF_TARGET STREQUAL "esp32")
    set(BOARD_COMPONENT board_cyd_2432s028)
else()
    message(FATAL_ERROR "espAGC: no board component for IDF_TARGET=${IDF_TARGET}")
endif()

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components"
    "${CMAKE_CURRENT_LIST_DIR}/boards/${BOARD_COMPONENT}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(espAGC)
```

- [ ] **Step 3: Verify T-Dongle build still succeeds**

Run: `idf.py fullclean && idf.py set-target esp32c5 && idf.py build`
Expected: build succeeds.

- [ ] **Step 4: Verify CYD build succeeds**

Run:
```powershell
idf.py -B build_cyd -D SDKCONFIG=sdkconfig.cyd set-target esp32
idf.py -B build_cyd -D SDKCONFIG=sdkconfig.cyd build
```
Expected: build succeeds (will produce a runnable binary; screen blank until panel impl in Task 16). Output ELF + bin in `build_cyd/`.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt boards/
git commit -m "build: select board component from IDF_TARGET; move boards/ out of components/"
```

---

## Phase 2 — ILI9341 panel driver

### Task 15: Create panel_ili9341.c

**Files:**
- Create: `components/display_hal/panel_ili9341.c`
- Create: `components/display_hal/include/ili9341_panel.h`
- Modify: `components/display_hal/CMakeLists.txt`

- [ ] **Step 1: Create the ILI9341 driver**

```c
// components/display_hal/panel_ili9341.c
//
// ILI9341 panel driver for the CYD-2432S028C. 240x320 native, rotated to
// 320x240 landscape via MADCTL. SPI2 host, 40 MHz. Backlight active-high
// on a separate GPIO (BOARD_LCD_BL).
//
// Reference: ILI9341 datasheet rev1.11, Adafruit_ILI9341 library, and the
// TFT_eSPI driver settings used by the CYD reference repo.

#include "ili9341_panel.h"
#include "display_panel_iface.h"
#include "board_pins.h"

#include <string.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"

#define ILI_HOST     SPI2_HOST
#define ILI_SPI_HZ   (40 * 1000 * 1000)

#define ILI_W  320
#define ILI_H  240

// Commands
#define ILI_SWRESET   0x01
#define ILI_SLPOUT    0x11
#define ILI_DISPON    0x29
#define ILI_CASET     0x2A
#define ILI_PASET     0x2B
#define ILI_RAMWR     0x2C
#define ILI_MADCTL    0x36
#define ILI_PIXFMT    0x3A

// MADCTL bits: MX | MV | BGR for landscape
#define MADCTL_LANDSCAPE 0x28      // MV=1, MY=0, MX=0, BGR=1 → top-left at native (0, 320), 320x240

static const char *TAG = "ili9341";

static spi_device_handle_t s_spi;

static void cs_low (void) { gpio_set_level(BOARD_LCD_CS, 0); }
static void cs_high(void) { gpio_set_level(BOARD_LCD_CS, 1); }
static void dc_low (void) { gpio_set_level(BOARD_LCD_DC, 0); }
static void dc_high(void) { gpio_set_level(BOARD_LCD_DC, 1); }

static void spi_tx(const uint8_t *bytes, size_t len)
{
    if (!len) return;
    spi_transaction_t t = { .length = len * 8, .tx_buffer = bytes };
    cs_low();
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
    cs_high();
}
static void wcmd(uint8_t cmd)                  { dc_low();  spi_tx(&cmd, 1); }
static void wdat(const uint8_t *d, size_t n)   { dc_high(); spi_tx(d, n); }

static void send_init(void)
{
    wcmd(ILI_SWRESET);              vTaskDelay(pdMS_TO_TICKS(120));
    wcmd(ILI_SLPOUT);               vTaskDelay(pdMS_TO_TICKS(120));
    wcmd(ILI_PIXFMT);               wdat((uint8_t[]){0x55}, 1);   // RGB565
    wcmd(ILI_MADCTL);               wdat((uint8_t[]){MADCTL_LANDSCAPE}, 1);
    wcmd(ILI_DISPON);               vTaskDelay(pdMS_TO_TICKS(20));
}

static void set_window(int x0, int y0, int x1, int y1)
{
    uint8_t c[4] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
    wcmd(ILI_CASET); wdat(c, 4);
    uint8_t r[4] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };
    wcmd(ILI_PASET); wdat(r, 4);
    wcmd(ILI_RAMWR);
}

esp_err_t ili9341_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BOARD_LCD_CS) | (1ULL << BOARD_LCD_DC),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio_config CS/DC");
    cs_high();

    spi_bus_config_t buscfg = {
        .mosi_io_num = BOARD_LCD_MOSI,
        .miso_io_num = BOARD_LCD_MISO,
        .sclk_io_num = BOARD_LCD_SCK,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = ILI_W * 80 * sizeof(uint16_t),   // one strip
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(ILI_HOST, &buscfg, SPI_DMA_CH_AUTO),
                        TAG, "spi_bus_initialize");

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = ILI_SPI_HZ, .mode = 0, .spics_io_num = -1,
        .queue_size = 4, .flags = SPI_DEVICE_NO_DUMMY,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(ILI_HOST, &devcfg, &s_spi),
                        TAG, "spi_bus_add_device");

    send_init();

    // Backlight on (active-high on CYD-C)
    gpio_config_t bl = { .pin_bit_mask = 1ULL << BOARD_LCD_BL, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&bl);
    gpio_set_level(BOARD_LCD_BL, 1);

    ESP_LOGI(TAG, "ILI9341 ready: %dx%d landscape", ILI_W, ILI_H);
    return ESP_OK;
}

esp_err_t ili9341_draw_rows(int y0, int y1, const uint16_t *pixels)
{
    if (y0 < 0 || y1 > ILI_H || y1 <= y0) return ESP_ERR_INVALID_ARG;

    set_window(0, y0, ILI_W - 1, y1 - 1);

    static uint16_t scratch[ILI_W];   // one row worth of byte-swapped pixels
    int rows = y1 - y0;
    dc_high(); cs_low();
    for (int y = 0; y < rows; y++) {
        const uint16_t *row = &pixels[y * ILI_W];
        for (int x = 0; x < ILI_W; x++) {
            uint16_t p = row[x];
            scratch[x] = (uint16_t)((p << 8) | (p >> 8));
        }
        spi_transaction_t t = { .length = ILI_W * 16, .tx_buffer = scratch };
        ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
    }
    cs_high();

    return ESP_OK;
}
```

- [ ] **Step 2: Create the iface adapter (small file alongside)**

Append at the bottom of `panel_ili9341.c`:

```c
// ---- iface adapter ----------------------------------------------------

const display_panel_iface_t display_panel_ili9341_cyd = {
    .width      = ILI_W,
    .height     = ILI_H,
    .swap_bytes = true,
    .init       = ili9341_init,
    .draw_rows  = ili9341_draw_rows,
};
```

- [ ] **Step 3: Create the public header**

```c
// components/display_hal/include/ili9341_panel.h
#pragma once
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ili9341_init(void);
esp_err_t ili9341_draw_rows(int y0, int y1, const uint16_t *pixels);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 4: Add to CMakeLists**

Modify `components/display_hal/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS         "display_hal.c" "st7735_panel.c" "panel_st7735.c"
                 "panel_ili9341.c" "font5x7.c"
                 "dsky_render_160x80.c" "dsky_layout.c"
    INCLUDE_DIRS "include"
    REQUIRES     channel_router log esp_driver_gpio esp_driver_spi esp_psram
)
```

The display_hal component now includes both panel drivers; the linker drops the unused one for each target as long as it isn't referenced.

We need access to `BOARD_LCD_*` pin defines from inside `panel_ili9341.c`. The cleanest way is to add `PRIV_REQUIRES` on whichever board component is selected — but that varies per target. Cleaner alternative: have `panel_ili9341.c` take pin numbers via `ili9341_init_with_pins(...)` instead of including `board_pins.h`. Let's do that.

- [ ] **Step 5: Refactor ili9341 to take pins via init param**

Rewrite the public header and the init function:

`components/display_hal/include/ili9341_panel.h`:

```c
#pragma once
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int sck, mosi, miso, cs, dc, rst, bl;
} ili9341_pins_t;

esp_err_t ili9341_init(const ili9341_pins_t *pins);
esp_err_t ili9341_draw_rows(int y0, int y1, const uint16_t *pixels);

#ifdef __cplusplus
}
#endif
```

In `panel_ili9341.c`, replace the `BOARD_LCD_*` references and the existing `ili9341_init()` with a pin-parametrized version:

```c
// at top, instead of #include "board_pins.h":
static ili9341_pins_t s_pins;

// rewrite the static helpers to use s_pins.cs / s_pins.dc:
static void cs_low (void) { gpio_set_level(s_pins.cs, 0); }
static void cs_high(void) { gpio_set_level(s_pins.cs, 1); }
static void dc_low (void) { gpio_set_level(s_pins.dc, 0); }
static void dc_high(void) { gpio_set_level(s_pins.dc, 1); }

// update ili9341_init signature:
esp_err_t ili9341_init(const ili9341_pins_t *pins)
{
    s_pins = *pins;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << s_pins.cs) | (1ULL << s_pins.dc),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio_config CS/DC");
    cs_high();

    spi_bus_config_t buscfg = {
        .mosi_io_num = s_pins.mosi,
        .miso_io_num = s_pins.miso,
        .sclk_io_num = s_pins.sck,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = ILI_W * 80 * sizeof(uint16_t),
    };
    // ... rest unchanged ...

    // Backlight init at the end:
    gpio_config_t bl = { .pin_bit_mask = 1ULL << s_pins.bl, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&bl);
    gpio_set_level(s_pins.bl, 1);

    ESP_LOGI(TAG, "ILI9341 ready: %dx%d landscape", ILI_W, ILI_H);
    return ESP_OK;
}
```

The iface adapter needs to bind to the right pins, but `display_panel_iface_t.init` takes no args. Solution: the board component ships its own thin wrapper that calls `ili9341_init(&pins)` with its pins, and exposes a custom iface instance:

Remove the `display_panel_ili9341_cyd` instance from `panel_ili9341.c` (delete those lines). The board component will create its own.

- [ ] **Step 6: Verify both builds still succeed**

Run:
```powershell
idf.py fullclean && idf.py set-target esp32c5 && idf.py build
idf.py -B build_cyd -D SDKCONFIG=sdkconfig.cyd build
```
Expected: both succeed (CYD still has no panel — board factory still returns NULL).

- [ ] **Step 7: Commit**

```bash
git add components/display_hal/panel_ili9341.c components/display_hal/include/ili9341_panel.h components/display_hal/CMakeLists.txt
git commit -m "display_hal: add pin-parametrized ILI9341 driver"
```

---

### Task 16: Wire CYD's board_get_panel() to ILI9341

**Files:**
- Modify: `boards/board_cyd_2432s028/board_init.c`

- [ ] **Step 1: Add the ILI9341 wrapper + iface in board_init.c**

Replace `boards/board_cyd_2432s028/board_init.c` with:

```c
// boards/board_cyd_2432s028/board_init.c
#include "board_pins.h"
#include "ili9341_panel.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "board";

static const ili9341_pins_t s_lcd_pins = {
    .sck  = BOARD_LCD_SCK,
    .mosi = BOARD_LCD_MOSI,
    .miso = BOARD_LCD_MISO,
    .cs   = BOARD_LCD_CS,
    .dc   = BOARD_LCD_DC,
    .rst  = BOARD_LCD_RST,
    .bl   = BOARD_LCD_BL,
};

static esp_err_t cyd_panel_init(void) { return ili9341_init(&s_lcd_pins); }

static const display_panel_iface_t s_panel = {
    .width      = BOARD_LCD_HRES,    // 320
    .height     = BOARD_LCD_VRES,    // 240
    .swap_bytes = true,
    .init       = cyd_panel_init,
    .draw_rows  = ili9341_draw_rows,
};

const display_panel_iface_t *board_get_panel(void) { return &s_panel; }
const panel_touch_iface_t   *board_get_touch(void) { return NULL; }   // TODO Task 19
const led_status_iface_t    *board_get_led(void)   { return NULL; }   // TODO Task 22

void board_init(void)
{
    gpio_config_t btn = {
        .pin_bit_mask = 1ULL << BOARD_BUTTON_BOOT,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn);

    ESP_LOGI(TAG, "%s board init complete", BOARD_NAME);
}
```

- [ ] **Step 2: Verify CYD build succeeds**

Run:
```powershell
idf.py -B build_cyd -D SDKCONFIG=sdkconfig.cyd build
```
Expected: succeeds. Note: `display_hal_init()` will now call into ILI9341 init, but `dsky_layout_for(320, 240)` returns NULL until Task 18 — so the splash will fail with "no DSKY layout for 320x240". That's expected — flash test happens after Task 18.

- [ ] **Step 3: Verify T-Dongle build still succeeds**

Run: `idf.py set-target esp32c5 && idf.py build`
Expected: succeeds.

- [ ] **Step 4: Commit**

```bash
git add boards/board_cyd_2432s028/board_init.c
git commit -m "board_cyd: wire ILI9341 panel through display_panel_iface"
```

---

## Phase 3 — 320×240 DSKY layout

### Task 17: Add Layer-1 host test for keypad hit_test

We can validate the keypad hit-test logic without hardware via a host gcc test alongside the existing tests in `tests/host/`.

**Files:**
- Create: `tests/host/test_keypad_hit.c`
- Modify: `tests/host/Makefile`

- [ ] **Step 1: Read existing host test structure**

Run: `cat tests/host/Makefile`
Expected: a Makefile that compiles each `test_*.c` against agc_engine.c and helpers, runs them sequentially.

- [ ] **Step 2: Define the planned 320x240 keypad geometry as constants both code paths share**

Create `components/display_hal/include/dsky_keypad_320x240.h`:

```c
// components/display_hal/include/dsky_keypad_320x240.h
#pragma once
//
// Keypad geometry for the 320x240 DSKY layout. Shared between the renderer
// (which draws the cells) and host-side tests (which verify hit-test).

#include <stdint.h>

#define DSKY_KP_X0       64        // keypad x range: [64, 320)
#define DSKY_KP_X1      320
#define DSKY_KP_Y0      100        // keypad y range: [100, 240)
#define DSKY_KP_Y1      240
#define DSKY_KP_COLS      6
#define DSKY_KP_ROWS      5
#define DSKY_KP_CW       42        // (320-64)/6 = 42.67 → 42 with 4 px slack
#define DSKY_KP_CH       28        // (240-100)/5 = 28

// Returns dsky_key_t (0..31) or -1 if (x,y) is outside the keypad.
int dsky_keypad_320x240_hit(int x, int y);
```

- [ ] **Step 3: Write the failing host test**

```c
// tests/host/test_keypad_hit.c
//
// Layer-1 unit test for the 320x240 DSKY keypad hit-test. Verifies a
// tap at the visual center of each cell maps to the right dsky_key_t.

#include <stdio.h>
#include <assert.h>
#include "dsky_keypad_320x240.h"

#include "../components/channel_router/include/dsky_keys.h"

#define X_OF(col)  (DSKY_KP_X0 + (col) * DSKY_KP_CW + DSKY_KP_CW / 2)
#define Y_OF(row)  (DSKY_KP_Y0 + (row) * DSKY_KP_CH + DSKY_KP_CH / 2)

int main(void)
{
    // Layout (col x row):
    //   0:VERB 1:+   2:7  3:8  4:9  5:CLR
    //   0:NOUN 1:-   2:4  3:5  4:6  5:PRO
    //   0:--   1:--  2:1  3:2  4:3  5:KEYREL
    //   0:--   1:0   2:-- 3:-- 4:-- 5:ENTR
    //   0:--   1:--  2:-- 3:-- 4:-- 5:RSET

    // Spot-check the corners of the keypad.
    assert(dsky_keypad_320x240_hit(X_OF(0), Y_OF(0)) == DSKY_KEY_VERB);
    assert(dsky_keypad_320x240_hit(X_OF(5), Y_OF(0)) == DSKY_KEY_CLR);
    assert(dsky_keypad_320x240_hit(X_OF(2), Y_OF(0)) == DSKY_KEY_7);
    assert(dsky_keypad_320x240_hit(X_OF(0), Y_OF(1)) == DSKY_KEY_NOUN);
    assert(dsky_keypad_320x240_hit(X_OF(1), Y_OF(0)) == DSKY_KEY_PLUS);
    assert(dsky_keypad_320x240_hit(X_OF(1), Y_OF(1)) == DSKY_KEY_MINUS);
    assert(dsky_keypad_320x240_hit(X_OF(1), Y_OF(3)) == DSKY_KEY_0);
    assert(dsky_keypad_320x240_hit(X_OF(5), Y_OF(3)) == DSKY_KEY_ENTR);
    assert(dsky_keypad_320x240_hit(X_OF(5), Y_OF(4)) == DSKY_KEY_RSET);
    assert(dsky_keypad_320x240_hit(X_OF(5), Y_OF(2)) == DSKY_KEY_KEYREL);
    assert(dsky_keypad_320x240_hit(X_OF(5), Y_OF(1)) == DSKY_KEY_PRO);

    // Out-of-bounds in all four directions returns -1.
    assert(dsky_keypad_320x240_hit(0, 0) == -1);
    assert(dsky_keypad_320x240_hit(63, 100) == -1);    // just left of keypad
    assert(dsky_keypad_320x240_hit(64, 99) == -1);     // just above keypad
    assert(dsky_keypad_320x240_hit(320, 240) == -1);   // just past bottom-right

    printf("test_keypad_hit OK\n");
    return 0;
}
```

- [ ] **Step 4: Add the test to tests/host/Makefile**

The hit-test gets its own .c file in Task 18 (`dsky_keypad_320x240.c`) — separate from the renderer so the host test doesn't pull in font code or RGB565 rendering. The Makefile rule links only that small file:

Inspect the existing Makefile, then add to the test list and add a build rule following the existing pattern. Example pattern (adapt to whatever the Makefile actually does for other tests):

```makefile
TESTS += test_keypad_hit

test_keypad_hit: test_keypad_hit.c ../../components/display_hal/dsky_keypad_320x240.c
	$(CC) $(CFLAGS) \
	    -I../../components/display_hal/include \
	    -I../../components/channel_router/include \
	    test_keypad_hit.c ../../components/display_hal/dsky_keypad_320x240.c \
	    -o $@
```

- [ ] **Step 5: Run the host tests; the new one should fail to build**

Run: `cd tests/host && mingw32-make run`
Expected: `test_keypad_hit` build fails — `dsky_keypad_320x240.c` doesn't exist yet, and `dsky_keypad_320x240_hit` is undeclared.

- [ ] **Step 6: Don't commit yet — Task 18 creates the file and makes the test pass.**

---

### Task 18: Implement dsky_render_320x240.c + dsky_keypad_320x240.c

**Files:**
- Create: `components/display_hal/dsky_keypad_320x240.c` (hit-test only — host-testable)
- Create: `components/display_hal/dsky_render_320x240.c` (renderer; calls into keypad table)
- Modify: `components/display_hal/dsky_layout.c`
- Modify: `components/display_hal/CMakeLists.txt`

The keypad cell table + hit-test live in their own .c file so the host test from Task 17 can link just that file (no font, no RGB565 renderer). The renderer .c includes the same header for cell metadata.

- [ ] **Step 1: Create the keypad hit-test (host-testable, no rendering)**

Add a tiny extension to `dsky_keypad_320x240.h` so the cell table is shared:

```c
// components/display_hal/include/dsky_keypad_320x240.h — append below existing
// content from Task 17:

typedef struct { int col, row; int code; const char *label; } dsky_kp_cell_t;
extern const dsky_kp_cell_t dsky_kp_cells_320x240[];
extern const int            dsky_kp_cells_320x240_count;
```

```c
// components/display_hal/dsky_keypad_320x240.c
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
    for (int i = 0; i < dsky_kp_cells_320x240_count; i++) {
        if (dsky_kp_cells_320x240[i].col == col &&
            dsky_kp_cells_320x240[i].row == row)
            return dsky_kp_cells_320x240[i].code;
    }
    return -1;
}
```

- [ ] **Step 2: Implement the renderer (uses the shared keypad table)**

```c
// components/display_hal/dsky_render_320x240.c
//
// 320x240 DSKY layout for the CYD-2432S028C. Faithful to the canonical
// Apollo DSKY: status panel left (60 px), display window upper-right
// (256x96), 19-key touch keypad lower-right (256x140, 6 cols x 5 rows).
//
// Rendered in three 80-row strips to fit the original ESP32's internal
// SRAM (no PSRAM on standard 2432S028C). strip_h = 80; the renderer
// only draws elements that intersect the current strip.

#include "dsky_layout.h"
#include "dsky_keypad_320x240.h"
#include "font5x7.h"

#include <stdio.h>
#include <string.h>

#define FB_W       320
#define FB_H       240
#define STRIP_H     80

// --- colors (RGB565) ---------------------------------------------------
#define COL_BG       0x0841
#define COL_AMBER    0xFD20
#define COL_AMBER_D  0x7A00
#define COL_GREEN    0x07E0
#define COL_RED      0xF800
#define COL_DIM      0x4208
#define COL_LIT_W    0xEF7D
#define COL_LIT_Y    0xFEA0
#define COL_PANEL    0x18C3

// --- status panel layout (left column) --------------------------------
//
// 2 columns × 7 rows of cells, 28x33 px each, in x=[0..56], y=[0..231].
// Left column white-lit indicators, right column yellow-lit conditions.

#define SP_X0   0
#define SP_W   60
#define SP_CELL_W  28
#define SP_CELL_H  33
#define SP_GAP_X    2

typedef struct {
    int row;
    const char *text;
    bool is_yellow;          // true = caution (right col), false = status (left col)
    // Index into the dsky_state_t flag bits we should test, or -1 for "always dim"
    int flag_offset;         // offset of bool field within dsky_state_t, -1 for always-dim
} sp_cell_t;

// We use offsetof-based addressing to read flags. This keeps the renderer
// table-driven.
#include <stddef.h>
#define FLAG(field) ((int)offsetof(dsky_state_t, field))

static const sp_cell_t sp_cells[] = {
    // left column (white-lit) — col 0
    { 0, "UPLINK\nACTY", false, FLAG(uplink_acty) },
    { 1, "NO ATT",       false, FLAG(no_att) },
    { 2, "STBY",         false, FLAG(stby) },
    { 3, "KEY REL",      false, FLAG(key_rel) },
    { 4, "OPR ERR",      false, FLAG(opr_err) },
    // right column (yellow-lit) — col 1
    { 0, "TEMP",         true,  FLAG(temp) },
    { 1, "GIMBAL\nLOCK", true,  FLAG(gimbal_lock) },
    { 2, "PROG",         true,  FLAG(prog_alarm) },
    { 3, "RESTART",      true,  FLAG(restart) },
    { 4, "TRACKER",      true,  FLAG(tracker) },
    { 5, "ALT",          true,  -1 },   // not driven by AGC engine
    { 6, "VEL",          true,  -1 },
};
#define SP_CELL_COUNT (sizeof(sp_cells) / sizeof(sp_cells[0]))

// --- pixel + text helpers ---------------------------------------------

static void put_pixel_strip(uint16_t *strip, int x, int y_local, uint16_t c)
{
    if ((unsigned)x < FB_W && (unsigned)y_local < STRIP_H) strip[y_local * FB_W + x] = c;
}

static void fill_rect_strip(uint16_t *strip, int x, int y_strip_local, int w, int h, uint16_t c)
{
    for (int yy = 0; yy < h; yy++) {
        int yl = y_strip_local + yy;
        if ((unsigned)yl >= STRIP_H) continue;
        for (int xx = 0; xx < w; xx++) {
            int xc = x + xx;
            if ((unsigned)xc >= FB_W) continue;
            strip[yl * FB_W + xc] = c;
        }
    }
}

static void draw_glyph_strip(uint16_t *strip, int x0, int y0_strip_local, int idx, uint16_t c)
{
    const uint8_t *g = font5x7[idx];
    for (int col = 0; col < FONT_W; col++) {
        uint8_t bits = g[col];
        for (int row = 0; row < FONT_H; row++) {
            if (bits & (1 << row)) put_pixel_strip(strip, x0 + col, y0_strip_local + row, c);
        }
    }
}

// Draw text supporting '\n' for two-line cell labels.
static void draw_text_strip(uint16_t *strip, int x0, int y0_strip_local, const char *s, uint16_t c)
{
    int x = x0, y = y0_strip_local;
    for (; *s; s++) {
        if (*s == '\n') { x = x0; y += FONT_H + 1; continue; }
        draw_glyph_strip(strip, x, y, font_index(*s), c);
        x += FONT_W + 1;
    }
}

// --- status panel render ----------------------------------------------

static void render_status_panel(uint16_t *strip, const dsky_state_t *s, int strip_y0)
{
    fill_rect_strip(strip, SP_X0, 0 - strip_y0, SP_W, FB_H, COL_PANEL);

    for (size_t i = 0; i < SP_CELL_COUNT; i++) {
        const sp_cell_t *c = &sp_cells[i];
        int col = c->is_yellow ? 1 : 0;
        int cx = SP_X0 + col * (SP_CELL_W + SP_GAP_X);
        int cy = c->row * SP_CELL_H;

        // strip-local coords:
        int cy_local = cy - strip_y0;
        // skip cells fully outside the strip:
        if (cy_local + SP_CELL_H <= 0 || cy_local >= STRIP_H) continue;

        bool lit = false;
        if (c->flag_offset >= 0) {
            const uint8_t *base = (const uint8_t *)s;
            lit = *(const bool *)(base + c->flag_offset);
        }
        uint16_t bg = lit ? (c->is_yellow ? COL_LIT_Y : COL_LIT_W) : COL_DIM;
        uint16_t fg = lit ? 0x0000 : (c->is_yellow ? COL_AMBER_D : 0x6B6D);

        fill_rect_strip(strip, cx + 1, cy_local + 1, SP_CELL_W - 2, SP_CELL_H - 2, bg);
        draw_text_strip(strip, cx + 3, cy_local + 4, c->text, fg);
    }
}

// --- register window render -------------------------------------------

#define RW_X0   64
#define RW_Y0    0
#define RW_W   256
#define RW_H    96

static char digit_char(int8_t d) { return d < 0 ? ' ' : (char)('0' + d); }
static char sign_char (int s)    {
    return s == DSKY_SIGN_PLUS ? '+' : s == DSKY_SIGN_MINUS ? '-' : ' ';
}

static void render_register_window(uint16_t *strip, const dsky_state_t *s, int strip_y0)
{
    fill_rect_strip(strip, RW_X0, RW_Y0 - strip_y0, RW_W, RW_H, 0x0000);

    char line[16];

    // COMP ACTY (top-left of window) — green dot when set.
    if (s->comp_acty)
        fill_rect_strip(strip, RW_X0 + 4, 4 - strip_y0, 6, 6, COL_GREEN);
    draw_text_strip(strip, RW_X0 + 14, 4 - strip_y0, "COMP\nACTY", COL_GREEN);

    // PROG label + value (top-right)
    draw_text_strip(strip, RW_X0 + 200, 4 - strip_y0, "PROG", COL_AMBER_D);
    snprintf(line, sizeof line, "%c%c", digit_char(s->prog[0]), digit_char(s->prog[1]));
    draw_text_strip(strip, RW_X0 + 232, 4 - strip_y0, line, COL_AMBER);

    // VERB / NOUN row
    draw_text_strip(strip, RW_X0 + 14, 22 - strip_y0, "VERB", COL_AMBER_D);
    snprintf(line, sizeof line, "%c%c", digit_char(s->verb[0]), digit_char(s->verb[1]));
    draw_text_strip(strip, RW_X0 + 50, 22 - strip_y0, line, COL_AMBER);

    draw_text_strip(strip, RW_X0 + 130, 22 - strip_y0, "NOUN", COL_AMBER_D);
    snprintf(line, sizeof line, "%c%c", digit_char(s->noun[0]), digit_char(s->noun[1]));
    draw_text_strip(strip, RW_X0 + 166, 22 - strip_y0, line, COL_AMBER);

    // R1 / R2 / R3
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

// --- keypad render (hit-test + cell table live in dsky_keypad_320x240.c) ---

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
        // label centered
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
    render_status_panel   (strip, s, y0);
    if (y0 < RW_H)        render_register_window(strip, s, y0);
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
```

- [ ] **Step 3: Register the layout in dsky_layout.c**

Modify `components/display_hal/dsky_layout.c`:

```c
#include "dsky_layout.h"
#include <stddef.h>

extern const dsky_layout_t dsky_layout_160x80;
extern const dsky_layout_t dsky_layout_320x240;

const dsky_layout_t *dsky_layout_for(int w, int h)
{
    if (w == 160 && h ==  80) return &dsky_layout_160x80;
    if (w == 320 && h == 240) return &dsky_layout_320x240;
    return NULL;
}
```

- [ ] **Step 4: Add new files to display_hal CMakeLists**

Modify `components/display_hal/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS         "display_hal.c" "st7735_panel.c" "panel_st7735.c"
                 "panel_ili9341.c" "font5x7.c"
                 "dsky_render_160x80.c" "dsky_render_320x240.c"
                 "dsky_keypad_320x240.c" "dsky_layout.c"
    INCLUDE_DIRS "include"
    REQUIRES     channel_router log esp_driver_gpio esp_driver_spi esp_psram
)
```

- [ ] **Step 5: Run host tests; the keypad test from Task 17 should now pass**

Run: `cd tests/host && mingw32-make run`
Expected: all four host tests including `test_keypad_hit` PASS.

- [ ] **Step 6: Verify both target builds**

Run:
```powershell
idf.py set-target esp32c5 && idf.py build
idf.py -B build_cyd -D SDKCONFIG=sdkconfig.cyd build
```
Expected: both succeed.

- [ ] **Step 7: Commit**

```bash
git add components/display_hal/dsky_keypad_320x240.c components/display_hal/dsky_render_320x240.c components/display_hal/include/dsky_keypad_320x240.h components/display_hal/dsky_layout.c components/display_hal/CMakeLists.txt tests/host/test_keypad_hit.c tests/host/Makefile
git commit -m "display_hal: 320x240 DSKY layout (renderer + keypad hit-test); host test"
```

- [ ] **Step 8: On-device check (optional, if hardware available)**

Flash CYD: `idf.py -B build_cyd -D SDKCONFIG=sdkconfig.cyd -p COM<n> flash monitor`
Expected on-device:
- Boot log: `display_hal up: 320x240, strip_h=80`
- Splash visible: "ESPAGC / BOOTING"
- After ROM loads: status panel left, registers upper-right, keypad lower-right
- Touching the screen does nothing yet (touch driver lands in Phase 4)

---

## Phase 4 — Touch input

### Task 19: Add CST820 driver + touch_input task

**Files:**
- Create: `components/touch_input/cst820.c`
- Create: `components/touch_input/touch_input.c`
- Create: `components/touch_input/include/touch_input.h`
- Create: `components/touch_input/Kconfig.projbuild`
- Modify: `components/touch_input/CMakeLists.txt`

- [ ] **Step 1: Create the public touch_input.h**

```c
// components/touch_input/include/touch_input.h
#pragma once
//
// touch_input — board-agnostic touchscreen-to-DSKY-key bridge.
// The board provides a panel_touch_iface_t; the active dsky_layout
// provides the hit-test. touch_input owns a low-priority FreeRTOS
// task that polls the touch iface at 50 Hz and posts decoded keys
// into channel_router.

#include "panel_touch_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*touch_hit_test_fn)(int x, int y);

// Starts the touch poll task. Must be called once after display_hal_init,
// since hit_test_fn comes from the active layout.
void touch_input_start(const panel_touch_iface_t *touch, touch_hit_test_fn hit_test);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Create touch_input.c (the poll-and-emit task)**

```c
// components/touch_input/touch_input.c
#include "touch_input.h"
#include "channel_router.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "touch";

static const panel_touch_iface_t *s_touch;
static touch_hit_test_fn          s_hit_test;

static void touch_task(void *arg)
{
    (void)arg;
    bool last_pressed = false;
    int64_t last_emit_us = 0;

    for (;;) {
        int x = 0, y = 0;
        bool pressed = s_touch->poll(&x, &y);
        int64_t now = esp_timer_get_time();
        if (pressed && !last_pressed && (now - last_emit_us) > 80000) {
            int code = s_hit_test(x, y);
            if (code >= 0) {
                channel_router_post_key(code);
                last_emit_us = now;
                ESP_LOGI(TAG, "tap (%d,%d) → key %d", x, y, code);
            }
        }
        last_pressed = pressed;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void touch_input_start(const panel_touch_iface_t *touch, touch_hit_test_fn hit_test)
{
    if (!touch || !hit_test) {
        ESP_LOGW(TAG, "touch_input disabled (touch=%p hit_test=%p)", touch, (void *)hit_test);
        return;
    }
    s_touch = touch;
    s_hit_test = hit_test;

    if (s_touch->init() != ESP_OK) {
        ESP_LOGE(TAG, "touch init failed");
        return;
    }
    xTaskCreate(touch_task, "touch", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "touch_input task up");
}
```

- [ ] **Step 3: Create the CST820 I2C driver**

```c
// components/touch_input/cst820.c
//
// CST820 capacitive touch driver for the CYD-2432S028C. I2C, 7-bit addr 0x15.
// Reports a single (x, y) point with sub-mm accuracy after a small reset
// pulse on the RST pin.
//
// Register 0x01 holds a "gesture/finger count" byte; non-zero = touch.
// Registers 0x03..0x06 hold the X/Y as two big-endian 12-bit values.

#include "panel_touch_iface.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "cst820";

#define CST820_ADDR 0x15

typedef struct { int sda, scl, rst, intr; } cst820_pins_t;
static cst820_pins_t s_pins;
static i2c_master_dev_handle_t s_dev;
static i2c_master_bus_handle_t s_bus;

static esp_err_t cst820_init_with_pins(const cst820_pins_t *p)
{
    s_pins = *p;

    gpio_config_t rst = { .pin_bit_mask = 1ULL << s_pins.rst, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&rst);
    gpio_set_level(s_pins.rst, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(s_pins.rst, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1,
        .sda_io_num = s_pins.sda,
        .scl_io_num = s_pins.scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bus_cfg, &s_bus) != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed");
        return ESP_FAIL;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = CST820_ADDR,
        .scl_speed_hz    = 400000,
    };
    if (i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev) != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "CST820 ready (sda=%d scl=%d rst=%d)", s_pins.sda, s_pins.scl, s_pins.rst);
    return ESP_OK;
}

static esp_err_t cst820_read(uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, 100);
}

static bool cst820_poll(int *x_out, int *y_out)
{
    uint8_t buf[6] = { 0 };
    if (cst820_read(0x01, buf, 6) != ESP_OK) return false;
    uint8_t finger_num = buf[0];
    if (finger_num == 0) return false;

    int x = ((buf[2] & 0x0F) << 8) | buf[3];
    int y = ((buf[4] & 0x0F) << 8) | buf[5];
    // Native CST820 reports 240(x) × 320(y) portrait. Rotate to landscape:
    // landscape_x = native_y, landscape_y = 240 - native_x
    *x_out = y;
    *y_out = 240 - x;
    return true;
}

// ---- iface adapter (with pin binding shim) ---------------------------

#include "board_pins.h"

static esp_err_t cst820_init_via_iface(void)
{
    cst820_pins_t pins = {
        .sda  = BOARD_TOUCH_SDA,
        .scl  = BOARD_TOUCH_SCL,
        .rst  = BOARD_TOUCH_RST,
        .intr = BOARD_TOUCH_INT,
    };
    return cst820_init_with_pins(&pins);
}

const panel_touch_iface_t panel_touch_cst820 = {
    .init = cst820_init_via_iface,
    .poll = cst820_poll,
};
```

- [ ] **Step 4: Create Kconfig for touch driver choice**

```
# components/touch_input/Kconfig.projbuild
menu "espAGC touch driver"

choice ESPAGC_CYD_TOUCH
    prompt "CYD touch controller"
    default ESPAGC_CYD_TOUCH_CST820
    help
        Selects the touch driver for ESP32-2432S028 variants.

config ESPAGC_CYD_TOUCH_CST820
    bool "CST820 (capacitive — 2432S028C)"

config ESPAGC_CYD_TOUCH_GT911
    bool "GT911 (capacitive — alternate)"

config ESPAGC_CYD_TOUCH_XPT2046
    bool "XPT2046 (resistive — 2432S028R)"

endchoice

endmenu
```

(Currently only `CST820` is implemented; the other two compile to nothing in v1 and are reserved for follow-ups. The choice still appears so the Kconfig key in `sdkconfig.defaults.esp32` is meaningful.)

- [ ] **Step 5: Update touch_input CMakeLists**

```cmake
# components/touch_input/CMakeLists.txt
idf_component_register(
    SRCS         "touch_input.c" "cst820.c"
    INCLUDE_DIRS "include"
    REQUIRES     channel_router log esp_driver_gpio esp_driver_i2c esp_timer freertos
)
```

Note: `cst820.c` includes `board_pins.h`, but the touch_input component itself doesn't depend on a specific board. The include works because `board_pins.h` lives in the active board component, which is in `EXTRA_COMPONENT_DIRS` and thus its include path is on the compile line. (T-Dongle's `board_pins.h` doesn't define `BOARD_TOUCH_*` — but T-Dongle never compiles `cst820.c`'s board-binding shim because `board_get_touch()` returns NULL and nothing references `panel_touch_cst820` on T-Dongle, so the linker drops it. Verify in step 6.)

Actually that's not quite right — the C file is compiled regardless of whether anything references it. We need `cst820.c` to NOT compile on T-Dongle. Two options: (a) gate it with `#ifdef IDF_TARGET_ESP32`, or (b) keep cst820 in a CYD-only sub-component. (a) is simpler.

Wrap the `#include "board_pins.h"` and the iface adapter at the bottom of `cst820.c` in:

```c
#if CONFIG_ESPAGC_CYD_TOUCH_CST820
#include "board_pins.h"
// ... cst820_init_via_iface and panel_touch_cst820 here ...
#endif
```

The `cst820_init_with_pins` and `cst820_poll` functions stay outside the guard so they're always compiled (just not referenced on T-Dongle, linker drops them).

Actually a cleaner final shape: leave `cst820.c` purely as `cst820_init_with_pins` and `cst820_poll`, with no board_pins.h reference; put the iface adapter in the **CYD board component** instead. That way touch_input/ stays pure (no board coupling) and the board component does its own wiring. Let's do that.

Final cst820.c (no iface adapter, no board_pins.h):

```c
// (cst820.c body without the iface adapter section — drop the
// `#include "board_pins.h"` at the bottom and the
// cst820_init_via_iface + panel_touch_cst820 definitions.
//
// Make cst820_init_with_pins and cst820_poll non-static so the board
// component can use them.)

esp_err_t cst820_init_with_pins(const cst820_pins_t *p);
bool      cst820_poll(int *x_out, int *y_out);
```

Add a small header `components/touch_input/include/cst820.h`:

```c
#pragma once
#include <stdbool.h>
#include "esp_err.h"

typedef struct { int sda, scl, rst, intr; } cst820_pins_t;

esp_err_t cst820_init_with_pins(const cst820_pins_t *p);
bool      cst820_poll(int *x_out, int *y_out);
```

And in `cst820.c`, change the typedef block to `#include "cst820.h"` and drop `static` from the two exported functions.

- [ ] **Step 6: Verify both target builds**

Run:
```powershell
idf.py set-target esp32c5 && idf.py build
idf.py -B build_cyd -D SDKCONFIG=sdkconfig.cyd build
```
Expected: both succeed.

- [ ] **Step 7: Commit**

```bash
git add components/touch_input/
git commit -m "touch_input: CST820 driver + 50 Hz poll task with debounce"
```

---

### Task 20: Wire CYD's board_get_touch() to CST820

**Files:**
- Modify: `boards/board_cyd_2432s028/board_init.c`
- Modify: `boards/board_cyd_2432s028/CMakeLists.txt`

- [ ] **Step 1: Add CST820 iface instance to board_init.c**

Add at the top of `boards/board_cyd_2432s028/board_init.c`:

```c
#include "cst820.h"
```

Add the iface adapter and update `board_get_touch()`:

```c
// (after existing static const display_panel_iface_t s_panel = ... block:)

static esp_err_t cyd_touch_init(void)
{
    cst820_pins_t p = {
        .sda  = BOARD_TOUCH_SDA,
        .scl  = BOARD_TOUCH_SCL,
        .rst  = BOARD_TOUCH_RST,
        .intr = BOARD_TOUCH_INT,
    };
    return cst820_init_with_pins(&p);
}

static const panel_touch_iface_t s_touch = {
    .init = cyd_touch_init,
    .poll = cst820_poll,
};

// Replace: const panel_touch_iface_t *board_get_touch(void) { return NULL; }
const panel_touch_iface_t *board_get_touch(void) { return &s_touch; }
```

- [ ] **Step 2: Verify CYD build succeeds**

Run: `idf.py -B build_cyd -D SDKCONFIG=sdkconfig.cyd build`
Expected: succeeds.

- [ ] **Step 3: Commit**

```bash
git add boards/board_cyd_2432s028/board_init.c
git commit -m "board_cyd: wire CST820 through panel_touch_iface"
```

---

### Task 21: app_main starts touch_input when board provides a touch iface

**Files:**
- Modify: `main/app_main.c`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: Read current app_main.c**

(Already read in Phase 0 prep — we'll add a small block after `dsky_input_start`.)

- [ ] **Step 2: Update app_main to start touch_input conditionally**

In `main/app_main.c`, add to the includes block:

```c
#include "display_panel_iface.h"
#include "touch_input.h"
#include "dsky_layout.h"
```

Add forward declarations for the board factories near the top of the file (just below the existing `static const char *TAG = "app";` line):

```c
extern const display_panel_iface_t *board_get_panel(void);
extern const panel_touch_iface_t   *board_get_touch(void);
```

In `app_main()`, after the existing `dsky_input_start(&in_cfg);` call, add:

```c
    // If this board has a touchscreen, also start the touch transport.
    // The active dsky_layout (selected by display_hal from panel resolution)
    // exposes the hit-test for its on-screen keypad.
    const panel_touch_iface_t *touch = board_get_touch();
    if (touch) {
        const display_panel_iface_t *panel = board_get_panel();
        const dsky_layout_t *layout = dsky_layout_for(panel->width, panel->height);
        if (layout && layout->hit_test) {
            touch_input_start(touch, layout->hit_test);
        } else {
            ESP_LOGW(TAG, "touch present but layout has no hit_test — skipping");
        }
    }
```

- [ ] **Step 3: Update main CMakeLists to require touch_input + display_hal**

Read `main/CMakeLists.txt`, then ensure `touch_input` and `display_hal` (and the board component dependency chain) are in `REQUIRES`. Likely already true for display_hal; add touch_input if missing.

Run: `cat main/CMakeLists.txt`

Edit to ensure REQUIRES includes `touch_input` and `display_hal`.

- [ ] **Step 4: Verify both target builds**

Run:
```powershell
idf.py set-target esp32c5 && idf.py build
idf.py -B build_cyd -D SDKCONFIG=sdkconfig.cyd build
```
Expected: both succeed. T-Dongle's `board_get_touch()` returns NULL so the touch block is skipped.

- [ ] **Step 5: On-device check (CYD, if hardware available)**

Flash CYD: `idf.py -B build_cyd -D SDKCONFIG=sdkconfig.cyd -p COM<n> flash monitor`
Expected:
- Boot log includes `CST820 ready (sda=33 scl=32 rst=25)` and `touch_input task up`.
- Tapping a digit cell on the screen prints `tap (x,y) → key N` in the monitor.
- The DSKY register area updates as the AGC engine receives the input.

- [ ] **Step 6: Commit**

```bash
git add main/app_main.c main/CMakeLists.txt
git commit -m "app_main: start touch_input when board provides a touch iface"
```

---

## Phase 5 — LED status on CYD

### Task 22: Implement rgb_gpio.c led_status driver

**Files:**
- Create: `components/led_status/rgb_gpio.c`
- Create: `components/led_status/include/rgb_gpio.h`
- Modify: `components/led_status/CMakeLists.txt`

- [ ] **Step 1: Create rgb_gpio.h**

```c
// components/led_status/include/rgb_gpio.h
#pragma once
#include <stdint.h>

typedef struct { int r, g, b; bool active_low; } rgb_gpio_pins_t;

void rgb_gpio_init_with_pins(const rgb_gpio_pins_t *p);
void rgb_gpio_set_rgb(uint8_t r, uint8_t g, uint8_t b);
```

- [ ] **Step 2: Create rgb_gpio.c**

```c
// components/led_status/rgb_gpio.c
//
// 3-GPIO RGB LED driver. Each color channel is bit-banged on/off (no PWM
// in v1 — just on/off thresholds at 0x80). Used by the CYD's onboard
// RGB LED, which is wired as 3 separate active-low GPIOs (4/16/17).

#include "rgb_gpio.h"
#include "driver/gpio.h"

static rgb_gpio_pins_t s_pins;

void rgb_gpio_init_with_pins(const rgb_gpio_pins_t *p)
{
    s_pins = *p;
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << s_pins.r) | (1ULL << s_pins.g) | (1ULL << s_pins.b),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cfg);
    rgb_gpio_set_rgb(0, 0, 0);
}

void rgb_gpio_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    int rh = (r >= 0x80) ? 1 : 0;
    int gh = (g >= 0x80) ? 1 : 0;
    int bh = (b >= 0x80) ? 1 : 0;
    int on  = s_pins.active_low ? 0 : 1;
    int off = s_pins.active_low ? 1 : 0;
    gpio_set_level(s_pins.r, rh ? on : off);
    gpio_set_level(s_pins.g, gh ? on : off);
    gpio_set_level(s_pins.b, bh ? on : off);
}
```

- [ ] **Step 3: Update led_status CMakeLists**

```cmake
idf_component_register(
    SRCS         "apa102_status.c" "apa102_iface.c" "rgb_gpio.c"
    INCLUDE_DIRS "include"
    REQUIRES     log esp_driver_gpio
)
```

- [ ] **Step 4: Verify both target builds**

Run:
```powershell
idf.py set-target esp32c5 && idf.py build
idf.py -B build_cyd -D SDKCONFIG=sdkconfig.cyd build
```
Expected: both succeed.

- [ ] **Step 5: Commit**

```bash
git add components/led_status/rgb_gpio.c components/led_status/include/rgb_gpio.h components/led_status/CMakeLists.txt
git commit -m "led_status: 3-GPIO RGB driver for CYD"
```

---

### Task 23: Wire CYD's board_get_led() to rgb_gpio

**Files:**
- Modify: `boards/board_cyd_2432s028/board_init.c`

- [ ] **Step 1: Add the LED iface adapter to board_init.c**

At the top:
```c
#include "rgb_gpio.h"
```

Add after the touch iface block:
```c
static void cyd_led_init(void)
{
    rgb_gpio_pins_t p = {
        .r = BOARD_LED_R, .g = BOARD_LED_G, .b = BOARD_LED_B,
        .active_low = true,
    };
    rgb_gpio_init_with_pins(&p);
}

static const led_status_iface_t s_led = {
    .init    = cyd_led_init,
    .set_rgb = rgb_gpio_set_rgb,
};

// Replace: const led_status_iface_t *board_get_led(void) { return NULL; }
const led_status_iface_t *board_get_led(void) { return &s_led; }
```

- [ ] **Step 2: Verify CYD build**

Run: `idf.py -B build_cyd -D SDKCONFIG=sdkconfig.cyd build`
Expected: succeeds.

- [ ] **Step 3: Commit**

```bash
git add boards/board_cyd_2432s028/board_init.c
git commit -m "board_cyd: wire RGB GPIO LED through led_status_iface"
```

---

## Phase 6 — Final integration polish

### Task 24: Have app_main initialize the LED + flash a boot heartbeat

**Files:**
- Modify: `main/app_main.c`

- [ ] **Step 1: Add LED init + boot heartbeat to app_main**

Add to the includes block:
```c
#include "led_status_iface.h"
```

Add a forward decl near the top:
```c
extern const led_status_iface_t *board_get_led(void);
```

In `app_main()`, between `board_init();` and `channel_router_init();`, add:

```c
    const led_status_iface_t *led = board_get_led();
    if (led) {
        led->init();
        led->set_rgb(0xFF, 0x80, 0x00);   // amber boot heartbeat
    }
```

And after `ESP_LOGI(TAG, "espAGC running");` (last line of app_main), add:
```c
    if (led) led->set_rgb(0x00, 0x40, 0x00);   // dim green = healthy
```

- [ ] **Step 2: Verify both target builds**

Run:
```powershell
idf.py set-target esp32c5 && idf.py build
idf.py -B build_cyd -D SDKCONFIG=sdkconfig.cyd build
```
Expected: both succeed.

- [ ] **Step 3: On-device check (both boards if available)**

Flash each, observe:
- **CYD**: onboard RGB LED briefly amber, then dim green after `espAGC running`.
- **T-Dongle**: APA102 RGB LED behavior is best-effort. `st7735_draw_rows` re-sends an APA102 off-frame on every panel write (load-bearing — see comment in `st7735_panel.c`), so any color we set via `led->set_rgb` will be overwritten on the next frame. That's the existing tension between the panel and LED sharing GPIO 4/5 — tracked as a follow-up in the spec ("led_status integration into the panel renderer"). For v1, T-Dongle's LED stays effectively off; this is acceptable, not a regression.

(If T-Dongle's APA102 starts flickering visibly after this change — i.e., a regression vs current `main` — revert the `led->set_rgb` calls in Step 1 and let the panel-driver-owned silence stand alone. The boot heartbeat is a CYD-only nicety in that case.)

- [ ] **Step 4: Commit**

```bash
git add main/app_main.c
git commit -m "app_main: amber boot / dim-green healthy LED heartbeat"
```

---

### Task 25: Add cyd.ps1 / cyd.bat helper scripts

**Files:**
- Create: `tools/cyd.ps1`
- Create: `tools/cyd.bat`

- [ ] **Step 1: Create the PowerShell wrapper**

```powershell
# tools/cyd.ps1
#
# Wrapper that pins the CYD build to its own sdkconfig + build dir,
# so day-to-day commands stay short:
#
#   .\tools\cyd.ps1 set-target esp32
#   .\tools\cyd.ps1 build
#   .\tools\cyd.ps1 -p COM5 flash monitor
#
param([Parameter(ValueFromRemainingArguments = $true)] [string[]] $Args)
idf.py -B build_cyd -D SDKCONFIG=sdkconfig.cyd @Args
exit $LASTEXITCODE
```

- [ ] **Step 2: Create the .bat wrapper**

```batch
@echo off
REM tools/cyd.bat — same as cyd.ps1 but for cmd.exe.
idf.py -B build_cyd -D SDKCONFIG=sdkconfig.cyd %*
```

- [ ] **Step 3: Commit**

```bash
git add tools/cyd.ps1 tools/cyd.bat
git commit -m "tools: cyd.{ps1,bat} wrappers for the CYD build dir + sdkconfig"
```

---

### Task 26: Update README for CYD

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update README**

Make these edits to `README.md`:

(a) Title — replace the first line:
```markdown
# espAGC — Apollo Guidance Computer on ESP32 (T-Dongle-C5 + CYD)
```

(b) Replace the `## Status` table's third row (`Layer 3 — hardware`) with one that reflects two boards:

```markdown
| Layer 3 — hardware | **boots, runs** | T-Dongle-C5 + CYD-2432S028C both bring up panel, run engine, expose web DSKY. CYD also feeds 19-key touch keypad into channel_router. |
```

(c) Replace the stale "opens an `espAGC` SoftAP" line with:
```markdown
joins the WiFi network configured in `idf.py menuconfig` → `espAGC WiFi` (or falls back to a SoftAP `espAGC` at `192.168.4.1` if no SSID is configured),
```

(d) In the `## Build & flash` section, after the existing T-Dongle block, add:

````markdown
### CYD (ESP32-2432S028C)

```powershell
# First time:
git submodule update --init third_party/CYD-reference

# Build / flash (the cyd.ps1 wrapper pins -B build_cyd -D SDKCONFIG=sdkconfig.cyd):
.\tools\cyd.ps1 set-target esp32
.\tools\cyd.ps1 build
.\tools\cyd.ps1 -p COM<n> flash monitor
```

The 320×240 DSKY uses the touchscreen for input (19-button keypad rendered in the lower-right). The same WiFi web DSKY at `http://<dongle-ip>/` works as a remote keypad.

> Targeting the resistive `2432S028R` variant or the GT911 capacitive variant is on the roadmap — see `components/touch_input/Kconfig.projbuild`.
````

(e) In `## Layout`, append after the existing `board_tdongle_c5/` line and remove that line, then add:

```markdown
boards/
  board_tdongle_c5/    Pin map + factories for LilyGO T-Dongle-C5 (ESP32-C5).
  board_cyd_2432s028/  Pin map + factories for CYD-2432S028C (ESP32, 2.8" capacitive).
```

(Update `components/board_tdongle_c5/` → `boards/board_tdongle_c5/` consistently.)

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "README: document CYD build target + WiFi STA-with-AP-fallback default"
```

---

## Self-review checklist

Run after the plan is fully executed:

- [ ] Layer 1 host tests still all pass (`cd tests/host && mingw32-make run` → 4/4 PASS including new `test_keypad_hit`).
- [ ] T-Dongle build: `idf.py set-target esp32c5 && idf.py build` → succeeds, on-device behavior unchanged from before this plan.
- [ ] CYD build: `.\tools\cyd.ps1 set-target esp32 && .\tools\cyd.ps1 build` → succeeds, on-device shows status panel + registers + touch keypad.
- [ ] No `#ifdef BOARD_*` or `#if CONFIG_IDF_TARGET_*` in shared code under `components/` (only in board components, panel/touch driver pin-binding shims, and the top-level `CMakeLists.txt`).
- [ ] `git grep -E "TODO|FIXME|XXX"` in modified files returns nothing surprising — only the deliberate `// TODO Task N` markers (which all got replaced when their tasks ran).
