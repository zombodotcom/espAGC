# espAGC — Cheap Yellow Display (ESP32-2432S028) board support

**Status:** design  
**Date:** 2026-05-09  
**Scope:** Add the ESP32-2432S028 ("Cheap Yellow Display" / CYD) as a second standalone build target alongside the existing LilyGO T-Dongle-C5. Single shared codebase; one `idf.py set-target` flips between them.

## Goal

A user with a CYD board can clone the repo, run `idf.py -B build_cyd set-target esp32 && idf.py -B build_cyd build flash monitor`, and get a fully functional espAGC: the AGC engine running, a 320×240 DSKY rendered on the ILI9341, the touchscreen acting as the 19-key DSKY keypad, and the existing WiFi web DSKY available as a remote input. Both Luminary099 and Comanche055 ROMs available, ROM selection via the BOOT button at reset (GPIO0).

## Non-goals (v1)

- SD-card features (the CYD's microSD slot stays unused, same as on T-Dongle).
- LVGL on either board. The existing direct-to-framebuffer renderer extends to 320×240; LVGL stays out.
- LR Altitude / LR Velocity caution lights (yellow, bottom of the real DSKY status panel) — the AGC engine doesn't emit them; they render dim and never light.
- Persistent NVS touch calibration. CST820 is reported in panel coordinates already; v1 uses a static identity mapping. (Resistive XPT2046 path, when added, will need calibration — out of scope until that variant is targeted.)
- Audio out (CYD speaker pin is wired but unused).
- Capacitive `GT911` and resistive `XPT2046` driver implementations land as follow-ups behind the same `panel_touch_iface_t`. v1 ships only `CST820` (the user's board variant) but the Kconfig choice and iface are in place from day one so the follow-ups are pure additions.

## Architecture

`app_main.c` already isolates board-specific concerns behind `display_hal_*` and `dsky_input_*`. The change introduces three thin C interfaces, two new component directories, and a build-time board selector keyed off `IDF_TARGET`. No `#ifdef BOARD_*` in shared code.

### Component layout (after the change)

```
components/
  agc_core/                  unchanged
  apollo_rom/                unchanged
  channel_router/            unchanged

  display_hal/               grows two panel impls + two DSKY layouts
    include/
      display_hal.h            unchanged public API
      display_panel_iface.h    NEW — { width, height, swap_bytes, init, draw_rows }
      dsky_layout.h            NEW — { fb_w, fb_h, init(fb), render(fb,state), hit_test(x,y) }
    panel_st7735.c             existing st7735_panel.c repackaged behind iface
    panel_ili9341.c            NEW
    dsky_render_160x80.c       existing display_hal.c logic, lifted out
    dsky_render_320x240.c      NEW — status panel + registers + on-screen keypad
    display_hal.c              shrinks to fb alloc + iface plumbing + dsky_layout_for(w,h)
    font5x7.{c,h}              unchanged

  touch_input/               NEW component
    include/
      touch_input.h            { touch_input_start(panel_touch_iface_t*, hit_test_fn) }
      panel_touch_iface.h      NEW — { init, poll → (x,y,pressed) }
    cst820.c                   I2C capacitive — CYD-2432S028C, **default**
    keypad_hit.c               (x,y) → dsky_key_t via the active layout's hit_test
    Kconfig.projbuild          choice ESPAGC_CYD_TOUCH { CST820, GT911, XPT2046 }
    # Follow-ups (NOT in v1):
    # gt911.c                  I2C capacitive — alternate C-variant
    # xpt2046.c                SPI resistive — CYD-2432S028R

  dsky_input/                unchanged. CYD config sets enable_usb_cdc=false.
  led_status/
    apa102_status.c            existing — T-Dongle
    rgb_gpio.c                 NEW — CYD's 3-GPIO active-low RGB LED
    include/led_status_iface.h NEW — { init, set_rgb }

  board_tdongle_c5/          unchanged pins; gains:
    board_init.c               implements board_get_panel/touch/led
                               (touch returns NULL — T-Dongle has no touchscreen)

  board_cyd_2432s028/        NEW
    include/board_pins.h
    board_init.c

main/                        unchanged code, only sees board_get_*() + ifaces
third_party/CYD-reference/   NEW submodule — witnessmenow/ESP32-Cheap-Yellow-Display
```

### Interfaces

**`display_hal/include/display_panel_iface.h`**
```c
typedef struct {
    int       width;          // pixels (panel coords, post-rotation)
    int       height;
    bool      swap_bytes;     // panel needs RGB565 byte-swapped on SPI
    esp_err_t (*init)(void);
    esp_err_t (*draw_rows)(int y0, int y1, const uint16_t *pixels);
} display_panel_iface_t;
```

**`touch_input/include/panel_touch_iface.h`**
```c
typedef struct {
    esp_err_t (*init)(void);
    bool (*poll)(int *x, int *y);   // returns true if currently pressed,
                                    // (*x,*y) in panel coords (post-rotation)
} panel_touch_iface_t;
```

**`led_status/include/led_status_iface.h`**
```c
typedef struct {
    void (*init)(void);
    void (*set_rgb)(uint8_t r, uint8_t g, uint8_t b);
} led_status_iface_t;
```

**`display_hal/include/dsky_layout.h`**
```c
typedef struct {
    int  fb_w, fb_h;
    int  strip_h;             // height of the scratch strip (== fb_h on 160x80,
                              // 80 on 320x240 — divides fb_h evenly)
    void (*init_strip)(uint16_t *strip, int y0);
    // Render rows [y0, y0+strip_h) into the caller-provided strip (strip_h
    // rows tall, fb_w wide). Layout draws only the elements that intersect
    // the strip.
    void (*render_strip)(uint16_t *strip, const dsky_state_t *s, int y0);
    // -1 if outside any button.
    int  (*hit_test)(int x, int y);
} dsky_layout_t;

const dsky_layout_t *dsky_layout_for(int w, int h);
```

`display_hal.c` allocates one `fb_w * strip_h * 2` scratch buffer and walks `y0 = 0, strip_h, 2*strip_h, …, fb_h - strip_h`, calling `render_strip` then `panel->draw_rows`. On 160×80, `strip_h == fb_h == 80` so this collapses to a single full-frame render — same behavior as today.

**Board factories (in each `board_pins.h`):**
```c
const display_panel_iface_t *board_get_panel(void);
const panel_touch_iface_t   *board_get_touch(void);   // NULL if absent
const led_status_iface_t    *board_get_led(void);     // NULL if absent
```

### Build-time board selection

Top-level `CMakeLists.txt`:
```cmake
if(IDF_TARGET STREQUAL "esp32c5")
    set(BOARD_COMPONENT board_tdongle_c5)
elseif(IDF_TARGET STREQUAL "esp32")
    set(BOARD_COMPONENT board_cyd_2432s028)
else()
    message(FATAL_ERROR "espAGC: no board for IDF_TARGET=${IDF_TARGET}")
endif()
list(APPEND EXTRA_COMPONENT_DIRS
     "${CMAKE_CURRENT_LIST_DIR}/components/${BOARD_COMPONENT}")
```

The unselected board component is simply not in `EXTRA_COMPONENT_DIRS`, so its sources never compile and there is no risk of pin-map cross-contamination.

### sdkconfig split

ESP-IDF auto-loads `sdkconfig.defaults.<IDF_TARGET>` after the common `sdkconfig.defaults`.

`sdkconfig.defaults` (common — strip board-specific knobs):
```
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
CONFIG_COMPILER_OPTIMIZATION_PERF=y
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=8
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=16
```

`sdkconfig.defaults.esp32c5` (T-Dongle):
```
CONFIG_IDF_TARGET="esp32c5"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_QUAD=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
```

`sdkconfig.defaults.esp32` (CYD):
```
CONFIG_IDF_TARGET="esp32"
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_ESPTOOLPY_FLASHMODE_DIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_40M=y
# no PSRAM on standard 2432S028C
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_cyd.csv"
CONFIG_ESPAGC_CYD_TOUCH_CST820=y
```

`partitions_cyd.csv` (4 MB flash variant of `partitions.csv`).

Build flow — each board pins its own `sdkconfig` file via `SDKCONFIG=`, so `set-target` for one board never clobbers the other's config:

```powershell
# T-Dongle (default sdkconfig — preserves existing single-board workflow)
idf.py set-target esp32c5
idf.py build flash monitor

# CYD — isolated sdkconfig.cyd + isolated build dir
idf.py -B build_cyd -D SDKCONFIG=sdkconfig.cyd set-target esp32
idf.py -B build_cyd -D SDKCONFIG=sdkconfig.cyd build flash monitor
```

A small `cyd.bat` / `cyd.ps1` wrapper hides the repeated `-B build_cyd -D SDKCONFIG=sdkconfig.cyd` so day-to-day commands stay short. The implementation will extend `.gitignore` from `sdkconfig` / `sdkconfig.old` to also cover `sdkconfig.cyd` (current `.gitignore` lists exact names, not a glob).

## CYD-2432S028C details

### Pin map

```c
// LCD: ILI9341, 240x320 native; we rotate to 320x240 landscape via MADCTL
#define BOARD_LCD_SCK    14
#define BOARD_LCD_MOSI   13
#define BOARD_LCD_MISO   12       // shared with SD slot only; touch is on its own bus
#define BOARD_LCD_CS     15
#define BOARD_LCD_DC      2
#define BOARD_LCD_RST    -1       // tied to EN — no GPIO
#define BOARD_LCD_BL     27       // PWM-able; moved here in C variant to free GPIO21

// Touch: CST820 capacitive (I2C, 7-bit addr 0x15)
#define BOARD_TOUCH_SDA  33
#define BOARD_TOUCH_SCL  32
#define BOARD_TOUCH_RST  25
#define BOARD_TOUCH_INT  21       // free in C variant because BL moved off

// RGB LED — 3 separate active-low GPIOs (NOT a chained driver)
#define BOARD_LED_R       4
#define BOARD_LED_G      16
#define BOARD_LED_B      17

// Boot button (pulled high; pressed = low)
#define BOARD_BUTTON_BOOT 0
```

Pin map cross-checked against the `witnessmenow/ESP32-Cheap-Yellow-Display` reference repo (added as a submodule under `third_party/CYD-reference/`).

### 320×240 DSKY layout

Mirrors the canonical Apollo DSKY: status panel left, register window upper-right, 19-key pad lower-right.

```
+----+-----------------------------------+
|stat|  COMP ACTY        PROG  13        |   y= 0..96
|    |  VERB 33    NOUN  13              |   register window
| 60w|  R1 +92311                        |   amber on dark
|    |  R2 +13270                        |
|240h|  R3 -46514                        |
|    +-----------------------------------+
|    |  V  +  7  8  9  C |
|    |  N  -  4  5  6  P |                   y= 100..240
|    |        1  2  3  K |                   keypad, 6 col x 5 row
|    |     0           E |                   cells ~43 x 28 px
|    |                 R |
+----+-----------------------------------+
 60         260
```

- **Status panel** (x=0..59, y=0..240): 2 columns × 7 rows of indicator cells, ~28×33 px each. Left column white-lit (`UPLINK ACTY`, `NO ATT`, `STBY`, `KEY REL`, `OPR ERR`, …), right column yellow-lit (`TEMP`, `GIMBAL LOCK`, `PROG`, `RESTART`, `TRACKER`, `ALT`, `VEL`). Drives off the existing `dsky_state_t` flag bits; LR-NoGood lights stay dim in v1.
- **Register window** (x=64..319, y=0..96): amber on dark, scaled font (~14 px digits, 7 px labels). `COMP ACTY` green dot top-left of the window.
- **Keypad** (x=64..319, y=100..240): 6 cols × 5 rows. `V`/`N` (VERB/NOUN) span 2 rows on the left edge; `+`/`−`/`0` form a thin signs/zero column; `7-9 / 4-6 / 1-3` form the 3×3 digit grid; `C/P/K/E/R` (CLR/PRO/KEY REL/ENTR/RSET) form the right column. Hit-test maps cells to `dsky_key_t` codes from `channel_router/include/dsky_keys.h`.
- **Color palette**: amber `0xFD20` (existing), green `0x07E0`, red `0xF800`, dim `0x4208`, white-lit `0xEF7D`, panel-bg `0x0841`.

### Framebuffer strategy

320 × 240 × 2 = 153,600 bytes. The standard 2432S028C has no PSRAM, and ESP32 internal SRAM is ~328 KB but heavily fragmented at boot — too risky to assume a contiguous 150 KB block.

`display_hal.c` allocates one `fb_w * strip_h * 2`-byte scratch strip (51,200 B for 320×240; 25,600 B for 160×80 today — same single allocation either way) and renders the frame in `fb_h / strip_h` passes:

```c
static uint16_t *strip;  // layout->fb_w * layout->strip_h * 2 bytes

void display_hal_update(const dsky_state_t *s)
{
    for (int y0 = 0; y0 < layout->fb_h; y0 += layout->strip_h) {
        layout->render_strip(strip, s, y0);
        panel->draw_rows(y0, y0 + layout->strip_h, strip);
    }
}
```

The 160×80 layout sets `strip_h = fb_h = 80`, so its render path collapses to a single full-frame call — identical work to today. The 320×240 layout sets `strip_h = 80`, gets three passes per frame at the 30 Hz UI tick.

### Touch input flow

A FreeRTOS task at priority 4 polls the CST820 over I2C at 50 Hz:

```
touch_task:
  loop:
    pressed = touch->poll(&x, &y)
    if pressed and not last_pressed and now - last_emit > 80ms:
        key = layout->hit_test(x, y)
        if key >= 0:
            channel_router_post_key(key)
            last_emit = now
    last_pressed = pressed
    delay 20 ms
```

Same `channel_router_post_key` path that USB-CDC and WiFi already use — the AGC engine cannot tell the three input transports apart.

### LED, ROM selection, WiFi

- **LED**: `rgb_gpio.c` configures GPIO 4/16/17 as outputs, drives them active-low. Same boot-time amber pulse as `apa102_status.c`.
- **ROM selection**: BOOT button (GPIO0) read at startup before `agc_core_init`. Held low → Comanche055; otherwise Luminary099. Identical mechanism to T-Dongle, just different pin.
- **WiFi**: zero changes. The existing `wifi_input.c` is STA-by-default with SoftAP fallback when `CONFIG_ESPAGC_WIFI_SSID` is empty. CYD inherits this verbatim.

## Testing

- **Layer 1 host tests** (`tests/host/`) stay green — none of them touch board, panel, or input layers. They keep building with `mingw32-make run`.
- **Compile-coverage**: CI (manual until a runner exists) builds both targets:
  ```
  idf.py set-target esp32c5 && idf.py build
  idf.py -B build_cyd set-target esp32 && idf.py -B build_cyd build
  ```
- **On-device manual checklist** for CYD: BOOT splash visible, "ESPAGC BOOTING" replaced by registers within 2 s, COMP ACTY blinks while engine ticks, touch a digit key → echoed in the register area, hold BOOT during reset → boot log shows `Comanche055`, WiFi joins / SoftAP appears, web DSKY POSTs still drive the same registers.

## Migration / rollback

The change is additive for T-Dongle. Existing pin maps, panel driver, input wiring, and `app_main` flow are preserved verbatim — only repackaged behind the new ifaces. Reverting the merge restores T-Dongle behavior exactly.

## Open follow-ups (not in v1, tracked for later)

1. **Resistive variant** (`CYD-2432S028R`, XPT2046): adds `xpt2046.c` behind the existing `panel_touch_iface_t` plus a 4-point calibration step. Pin map differs (BL on 21, touch on its own SPI bus 25/32/39/33, no INT on 21).
2. **GT911 capacitive**: same shape as CST820 — different I2C protocol. Selectable via the same Kconfig choice.
3. **NVS-persisted touch calibration** for the resistive path.
4. **README refresh**: drop the stale "SoftAP" claim (actual default is STA with SoftAP fallback) and add CYD build/flash instructions.
5. **led_status integration into the panel renderer** — the T-Dongle APA102 is currently silenced because GPIO 4/5 belong to the panel; once the panel/LED ownership is cleaner, restore status indication.
