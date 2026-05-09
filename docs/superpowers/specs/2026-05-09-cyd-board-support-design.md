# espAGC — Cheap Yellow Display (ESP32-2432S028C) port

**Status:** design (revised — single target)
**Date:** 2026-05-09
**Scope:** Port espAGC to the ESP32-2432S028C ("Cheap Yellow Display" / CYD, 2.8" capacitive). Delete the LilyGO T-Dongle-C5 target — no second-board support. Single chip target (`esp32`), single sdkconfig.

## Goal

Clone the repo, run `idf.py set-target esp32 && idf.py build flash monitor`, and get a fully functional espAGC: yaAGC engine running, a 320×240 DSKY rendered on the ILI9341, the CST820 touchscreen acting as the 19-key DSKY keypad, the existing WiFi web DSKY available as a remote keypad, and the onboard RGB LED used as a status indicator. Both Luminary099 and Comanche055 ROMs available, ROM selection via the BOOT button at reset (GPIO0).

## Non-goals

- T-Dongle-C5 (ESP32-C5) support. Removed entirely from the tree.
- Resistive XPT2046 variant (`CYD-2432S028R`). Out of scope; behind a Kconfig follow-up.
- Capacitive `GT911` variant. Out of scope; behind a Kconfig follow-up.
- LVGL. The direct-to-framebuffer renderer scales fine to 320×240.
- SD card use, audio out, persistent NVS touch calibration.
- LR Altitude / LR Velocity caution lights — the AGC engine doesn't emit them; they render permanently dim.

## Architecture

`app_main.c` flow stays identical to today: `board_init → channel_router_init → display_hal_init → load ROM → agc_core_init → dsky_input_start → spawn agc + ui tasks`. The only structural change is that `display_hal` and `dsky_input` consume thin board-provided interfaces instead of hardcoded ST7735+APA102 paths.

Three thin C interfaces:
- `display_panel_iface_t` (panel: width, height, init, draw_rows)
- `panel_touch_iface_t` (touch: init, poll → x,y,pressed)
- `led_status_iface_t` (LED: init, set_rgb)

Plus a renderer iface keyed by panel resolution:
- `dsky_layout_t` (fb_w, fb_h, strip_h, init_strip, render_strip, hit_test)

The board component (`board_cyd_2432s028`) returns concrete instances via `board_get_panel/touch/led`. Even with one board today, the iface keeps `display_hal` board-agnostic — it's the same shape `display_hal.h` already promised in its existing header comment ("new panels … drop in as additional panel_*.c files implementing display_hal_panel_iface_t").

### Component layout

```
components/
  agc_core/                  unchanged — yaAGC engine wrapper
  apollo_rom/                unchanged — yaYUL → Luminary099 / Comanche055
  channel_router/            unchanged — AGC IO ↔ dsky_state

  display_hal/               new ifaces, single panel impl, single layout
    include/
      display_hal.h            unchanged public API (init + update)
      display_panel_iface.h    NEW
      dsky_layout.h            NEW
      dsky_keypad_320x240.h    NEW (geometry + cell table)
      ili9341_panel.h          NEW (panel-driver public)
    panel_ili9341.c            NEW — pin-parametrized ILI9341 driver
    dsky_render_320x240.c      NEW — status panel + registers + on-screen keypad
    dsky_keypad_320x240.c      NEW — keypad cell table + hit-test (host-testable)
    dsky_layout.c              NEW — resolution → renderer registry
    display_hal.c              NEW — panel + layout glue, strip-based draw loop
    font5x7.{c,h}              unchanged

  touch_input/               NEW component
    include/
      touch_input.h
      panel_touch_iface.h      NEW
      cst820.h                 NEW (driver public)
    cst820.c                   I2C capacitive driver
    touch_input.c              50 Hz poll task → channel_router_post_key
    Kconfig.projbuild          choice ESPAGC_CYD_TOUCH { CST820, GT911, XPT2046 }

  dsky_input/                unchanged. CYD config sets enable_usb_cdc=false.

  led_status/                rewritten for CYD's 3-GPIO RGB LED
    include/
      led_status_iface.h       NEW
      rgb_gpio.h               NEW
    rgb_gpio.c                 NEW — 3-GPIO active-low RGB driver

main/                        unchanged code, only sees board_get_*() + ifaces

boards/
  board_cyd_2432s028/        NEW — pin map + factory functions
    include/board_pins.h
    board_init.c

third_party/
  CYD-reference/             NEW submodule — witnessmenow/ESP32-Cheap-Yellow-Display
  Apollo-11/                 unchanged
  virtualagc/                unchanged
  T-Dongle-C5/               REMOVED

Removed entirely:
  components/board_tdongle_c5/
  components/display_hal/st7735_panel.c (and st7735_panel.h)
  components/led_status/apa102_status.c
```

### Interfaces

**`display_hal/include/display_panel_iface.h`**
```c
typedef struct {
    int       width;
    int       height;
    bool      swap_bytes;     // informational
    esp_err_t (*init)(void);
    esp_err_t (*draw_rows)(int y0, int y1, const uint16_t *pixels);
} display_panel_iface_t;
```

**`touch_input/include/panel_touch_iface.h`**
```c
typedef struct {
    esp_err_t (*init)(void);
    bool (*poll)(int *x, int *y);
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
    int  strip_h;             // divides fb_h evenly
    void (*init_strip)(uint16_t *strip, int y0);
    void (*render_strip)(uint16_t *strip, const dsky_state_t *s, int y0);
    int  (*hit_test)(int x, int y);
} dsky_layout_t;

const dsky_layout_t *dsky_layout_for(int w, int h);
```

**Board factories (`boards/board_cyd_2432s028/include/board_pins.h`):**
```c
const display_panel_iface_t *board_get_panel(void);
const panel_touch_iface_t   *board_get_touch(void);
const led_status_iface_t    *board_get_led(void);
```

### Pin map (CYD-2432S028C)

```c
// LCD: ILI9341, 240x320 native; rotated to 320x240 landscape via MADCTL
#define BOARD_LCD_HRES   320
#define BOARD_LCD_VRES   240
#define BOARD_LCD_SCK    14
#define BOARD_LCD_MOSI   13
#define BOARD_LCD_MISO   12      // shared with SD slot
#define BOARD_LCD_CS     15
#define BOARD_LCD_DC      2
#define BOARD_LCD_RST    -1      // tied to EN; no GPIO control
#define BOARD_LCD_BL     27      // active-high backlight (C variant pin)

// Touch: CST820 capacitive (I2C, 7-bit addr 0x15)
#define BOARD_TOUCH_SDA  33
#define BOARD_TOUCH_SCL  32
#define BOARD_TOUCH_RST  25
#define BOARD_TOUCH_INT  21      // free in C variant because BL moved off

// RGB LED — 3 separate active-low GPIOs
#define BOARD_LED_R       4
#define BOARD_LED_G      16
#define BOARD_LED_B      17

// SD card (unused).
#define BOARD_SD_CS       5

// Boot button (pulled high; pressed = low) — standard ESP32 BOOT.
#define BOARD_BUTTON_BOOT 0
```

Pin map cross-checked against the `witnessmenow/ESP32-Cheap-Yellow-Display` reference repo (added as a submodule under `third_party/CYD-reference/`).

### sdkconfig (single target, no per-target split)

```
CONFIG_IDF_TARGET="esp32"

CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_ESPTOOLPY_FLASHMODE_DIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_40M=y

CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
CONFIG_COMPILER_OPTIMIZATION_PERF=y

CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=8
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=16

CONFIG_ESPAGC_CYD_TOUCH_CST820=y
```

`partitions.csv` is rewritten for 4 MB total flash (the existing 16 MB layout is dropped along with T-Dongle).

Build flow stays simple:
```powershell
git clone --recurse-submodules https://github.com/zombodotcom/espAGC.git
cd espAGC
. C:\esp\v6.0.1\esp-idf\export.ps1
idf.py set-target esp32
idf.py build flash monitor
```

### 320×240 DSKY layout

Mirrors the canonical Apollo DSKY: status panel left, register window upper-right, 19-key pad lower-right.

```
+----+-----------------------------------+
|stat|  COMP ACTY        PROG  13        |   y= 0..96    register window
|    |  VERB 33    NOUN  13              |   amber on dark
| 60w|  R1 +92311                        |
|    |  R2 +13270                        |
|240h|  R3 -46514                        |
|    +-----------------------------------+
|    |  V  +  7  8  9  C |
|    |  N  -  4  5  6  P |                   y= 100..240  keypad
|    |        1  2  3  K |                   6 col x 5 row, ~43 x 28 px
|    |     0           E |
|    |                 R |
+----+-----------------------------------+
 60         260
```

- **Status panel** (x=0..59, y=0..240): 2 columns × 7 rows of indicator cells, ~28×33 px each. Left column white-lit (`UPLINK ACTY`, `NO ATT`, `STBY`, `KEY REL`, `OPR ERR`); right column yellow-lit (`TEMP`, `GIMBAL LOCK`, `PROG`, `RESTART`, `TRACKER`, `ALT`, `VEL`). Driven from `dsky_state_t` flags.
- **Register window** (x=64..319, y=0..96): amber on dark, scaled font (~14 px digits, 7 px labels). `COMP ACTY` green dot top-left.
- **Keypad** (x=64..319, y=100..240): 6 cols × 5 rows. Hit-test maps cells to `dsky_key_t` codes from `channel_router/include/dsky_keys.h`.
- **Color palette**: amber `0xFD20`, green `0x07E0`, red `0xF800`, dim `0x4208`, white-lit `0xEF7D`, panel-bg `0x18C3`.

### Framebuffer strategy

320 × 240 × 2 = 153,600 bytes. The standard 2432S028C has no PSRAM; ESP32 internal SRAM is ~328 KB but heavily fragmented at boot — too risky to assume a contiguous 150 KB block.

`display_hal.c` allocates one `fb_w * strip_h * 2`-byte scratch strip (51,200 B for 320×240) and renders the frame in three 80-row passes:

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

The 320×240 layout sets `strip_h = 80`, gets three passes per frame at the 30 Hz UI tick.

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

Same `channel_router_post_key` path that WiFi already uses — the AGC engine cannot tell input transports apart.

### LED, ROM selection, WiFi

- **LED**: `rgb_gpio.c` configures GPIO 4/16/17 as active-low outputs. `app_main` shows amber at boot, dim green once `espAGC running`.
- **ROM selection**: BOOT button (GPIO0) read at startup. Held low → Comanche055; otherwise Luminary099.
- **WiFi**: zero changes. The existing `wifi_input.c` is STA-by-default with SoftAP fallback when `CONFIG_ESPAGC_WIFI_SSID` is empty. Inherits verbatim.
- **USB-Serial-JTAG input**: removed. The CYD has only a CP2102 USB-UART, no USB-Serial-JTAG port. `dsky_input_config` sets `enable_usb_cdc = false` in `app_main.c`.

## Testing

- **Layer 1 host tests** (`tests/host/`) keep working — none touch board, panel, or input layers. Add a new host test for the keypad hit-test logic (pure function, no hardware needed).
- **On-device manual checklist**: BOOT splash visible, "ESPAGC BOOTING" replaced by registers within 2 s, COMP ACTY blinks while engine ticks, touching a digit cell prints `tap (x,y) → key N` in the monitor and updates the register area, hold BOOT during reset → boot log shows `Comanche055`, WiFi joins / SoftAP appears, web DSKY POSTs still drive the same registers.

## Migration (from current main)

Destructive — the current ESP32-C5 firmware is replaced. Anyone wanting the old T-Dongle behavior reverts to `0a19f6d` (the last commit before this work) or earlier.

## Open follow-ups

1. **Resistive variant** (`CYD-2432S028R`, XPT2046 over SPI): adds `xpt2046.c` behind the existing `panel_touch_iface_t` plus a 4-point calibration step. Pin map differs (BL on 21, touch on its own SPI bus 25/32/39/33).
2. **GT911 capacitive variant**: same shape as CST820 — different I2C protocol. Selectable via the same Kconfig choice.
3. **NVS-persisted touch calibration** for the resistive path.
