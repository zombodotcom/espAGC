# CYD Port Implementation Plan (revised — single target)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port espAGC from LilyGO T-Dongle-C5 (ESP32-C5) to ESP32-2432S028C (Cheap Yellow Display, ESP32, 2.8" capacitive). Delete the T-Dongle target entirely; CYD becomes the only target.

**Architecture:** Three thin C interfaces (`display_panel_iface_t`, `panel_touch_iface_t`, `led_status_iface_t`) consumed by `display_hal`, `touch_input`, and `app_main`. A new `boards/board_cyd_2432s028/` returns concrete impls. `display_hal.c` allocates one strip-sized scratch buffer and renders the 320×240 frame in three 80-row passes. CST820 capacitive touch hits a precomputed keypad cell table, posts decoded keys via the existing `channel_router_post_key` path.

**Tech Stack:** ESP-IDF v6.0+ (esp32 target), C11, FreeRTOS, ILI9341 (SPI), CST820 (I2C), direct-to-framebuffer rendering (no LVGL).

**Spec:** `docs/superpowers/specs/2026-05-09-cyd-board-support-design.md`

---

## Phase 1 — Demolition (delete T-Dongle code)

After Phase 1 the build is broken (no panel driver, no board component) but the tree is clean of T-Dongle-specific code, ready for the CYD pieces to land in their own files.

### Task 1: Delete T-Dongle source files

**Files:**
- Delete: `components/board_tdongle_c5/` (entire dir)
- Delete: `components/display_hal/st7735_panel.c`
- Delete: `components/display_hal/include/st7735_panel.h`
- Delete: `components/led_status/apa102_status.c`

`led_status` currently has no header/iface, just the legacy `.c` file (which `app_main` already has commented out — see `main/app_main.c:24-27`). The component dir survives Phase 1; we'll replace its body in Phase 5.

- [ ] **Step 1: Verify the files exist (sanity-check before deletion)**

Run:
```bash
ls -la components/board_tdongle_c5/ components/display_hal/st7735_panel.c components/display_hal/include/st7735_panel.h components/led_status/apa102_status.c
```
Expected: each path lists.

- [ ] **Step 2: Delete the files**

Run:
```bash
git rm -r components/board_tdongle_c5
git rm components/display_hal/st7735_panel.c
git rm components/display_hal/include/st7735_panel.h
git rm components/led_status/apa102_status.c
```

- [ ] **Step 3: Verify nothing in the tree still references them**

Run:
```bash
git grep -nE "board_tdongle_c5|st7735_panel|apa102_status" -- ':!docs/' ':!third_party/' ':!.claude/'
```
Expected: only matches inside `display_hal.c`, `display_hal/CMakeLists.txt`, `led_status/CMakeLists.txt`, `main/app_main.c`. (No matches in `docs/`, `third_party/`, or `.claude/` — those exclusions filter out historic mentions in spec/plan + the T-Dongle reference submodule itself.)

- [ ] **Step 4: Commit**

```bash
git commit -m "remove T-Dongle-C5 target (board, ST7735 driver, APA102 driver)"
```

---

### Task 2: Strip remaining T-Dongle references from CMake + display_hal.c + app_main.c

**Files:**
- Modify: `components/display_hal/CMakeLists.txt`
- Modify: `components/display_hal/display_hal.c` (gut it — full replacement comes in Task 12)
- Modify: `components/led_status/CMakeLists.txt` (empty SRCS for now)
- Modify: `main/app_main.c` (drop the led_status comment block)

This task gets the build to "fails because there's no panel driver" rather than "fails because of dangling references". Don't try to make it compile — Phase 2-5 will.

- [ ] **Step 1: Replace display_hal/CMakeLists.txt with a minimal stub**

```cmake
# components/display_hal/CMakeLists.txt
idf_component_register(
    SRCS         "display_hal.c" "font5x7.c"
    INCLUDE_DIRS "include"
    REQUIRES     channel_router log esp_driver_gpio esp_driver_spi esp_psram
)
```

- [ ] **Step 2: Replace display_hal.c with an empty stub**

```c
// components/display_hal/display_hal.c
//
// Stubbed during the CYD port. Real impl lands in Task 12.

#include "display_hal.h"

void display_hal_init(void)               {}
void display_hal_update(const dsky_state_t *s) { (void)s; }
```

- [ ] **Step 3: Replace led_status/CMakeLists.txt with an empty stub**

```cmake
# components/led_status/CMakeLists.txt
idf_component_register(
    SRCS         ""
    INCLUDE_DIRS "include"
)
```

(Empty SRCS is legal — IDF treats it as a header-only component. We'll add `rgb_gpio.c` in Phase 5.)

- [ ] **Step 4: Strip the led_status comment block from app_main.c**

Remove these four lines from `main/app_main.c`:
```c
// led_status driver disabled: the ST7735 panel driver owns GPIO 4/5 (APA102)
// and silences the LED to avoid SPI-coupled flicker. Re-enable once we
// integrate status indication into the panel renderer.
// #include "led_status.h"
```

(Replaced with a real `#include "led_status_iface.h"` in Task 16.)

- [ ] **Step 5: Verify host tests still pass**

```bash
cd tests/host && make run
```
Expected: `ALL PASS` — host tests don't touch board/panel/LED code.

- [ ] **Step 6: Commit**

```bash
git add components/display_hal/CMakeLists.txt components/display_hal/display_hal.c components/led_status/CMakeLists.txt main/app_main.c
git commit -m "stub display_hal + led_status; remove T-Dongle led_status comment"
```

---

### Task 3: Drop T-Dongle-C5 reference submodule

**Files:**
- Modify: `.gitmodules`
- Delete: `third_party/T-Dongle-C5/` (the submodule checkout)

- [ ] **Step 1: Check current submodule state**

Run: `git submodule status`
Expected output: lists `third_party/T-Dongle-C5`, `third_party/Apollo-11`, `third_party/virtualagc` (initialized or not).

- [ ] **Step 2: Remove the T-Dongle-C5 submodule**

Run:
```bash
git submodule deinit -f third_party/T-Dongle-C5
git rm -f third_party/T-Dongle-C5
rm -rf .git/modules/third_party/T-Dongle-C5
```

- [ ] **Step 3: Verify .gitmodules no longer mentions T-Dongle-C5**

Run: `cat .gitmodules`
Expected: only `Apollo-11` and `virtualagc` entries remain.

- [ ] **Step 4: Commit**

```bash
git add .gitmodules
git commit -m "third_party: drop T-Dongle-C5 submodule"
```

---

## Phase 2 — Single-target build (esp32, CYD partition layout)

After Phase 2 the build configuration is fully retargeted to ESP32 / 4MB flash. The build still doesn't link (no panel driver), but `idf.py set-target esp32` works cleanly.

### Task 4: Rewrite sdkconfig.defaults for ESP32 (4MB flash, no PSRAM)

**Files:**
- Modify: `sdkconfig.defaults`

- [ ] **Step 1: Replace sdkconfig.defaults entirely**

```
CONFIG_IDF_TARGET="esp32"

# Flash (CYD-2432S028C: 4 MB DIO flash, no PSRAM)
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_ESPTOOLPY_FLASHMODE_DIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_40M=y

# Custom partition table
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# Larger main task stack
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192

CONFIG_LOG_DEFAULT_LEVEL_INFO=y
CONFIG_COMPILER_OPTIMIZATION_PERF=y

# WiFi
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=8
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=16

# Touch driver
CONFIG_ESPAGC_CYD_TOUCH_CST820=y
```

- [ ] **Step 2: Reset target to esp32 (clears stale C5 sdkconfig)**

Run:
```bash
rm -f sdkconfig sdkconfig.old
idf.py set-target esp32 2>&1 | tail -20
```
Expected: target sets to esp32 successfully (the build will still error later — that's expected).

- [ ] **Step 3: Commit**

```bash
git add sdkconfig.defaults
git commit -m "sdkconfig: retarget to ESP32 (CYD); 4MB DIO flash; CST820 touch default"
```

---

### Task 5: Rewrite partitions.csv for 4MB flash

**Files:**
- Modify: `partitions.csv`

- [ ] **Step 1: Read the current partitions.csv to see its layout**

Run: `cat partitions.csv`
Take note of partition names (factory, ota_*, nvs, phy_init, storage, etc.) so the new layout preserves the names the firmware references.

- [ ] **Step 2: Write a 4MB-fitted layout**

A typical ESP32-2432S028C-friendly layout:

```
# Name,   Type, SubType, Offset,  Size,    Flags
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000, 0x3F0000,
```

If the existing `partitions.csv` had additional entries (e.g., a `storage` data partition), drop them or scale to fit 4 MB total — the firmware doesn't use storage in v1.

- [ ] **Step 3: Commit**

```bash
git add partitions.csv
git commit -m "partitions: 4MB flash layout for CYD"
```

---

### Task 6: Add witnessmenow CYD reference as submodule

**Files:**
- Modify: `.gitmodules`
- Add: `third_party/CYD-reference/`

- [ ] **Step 1: Add the submodule**

Run:
```bash
git submodule add https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display.git third_party/CYD-reference
```

- [ ] **Step 2: Spot-check that the README is present**

Run: `ls third_party/CYD-reference/README.md`
Expected: lists. (Used as documentation only; nothing in this repo compiles from it.)

- [ ] **Step 3: Commit**

```bash
git add .gitmodules third_party/CYD-reference
git commit -m "third_party: add witnessmenow/ESP32-Cheap-Yellow-Display as submodule"
```

---

### Task 7: Top-level CMakeLists picks up boards/board_cyd_2432s028

**Files:**
- Modify: `CMakeLists.txt`

The board component will live at `boards/board_cyd_2432s028/` (created in Task 8). The top-level CMake just adds that path to `EXTRA_COMPONENT_DIRS` — no `IDF_TARGET` switching since there's only one target.

- [ ] **Step 1: Replace CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components"
    "${CMAKE_CURRENT_LIST_DIR}/boards/board_cyd_2432s028")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(espAGC)
```

- [ ] **Step 2: Commit (no build verification yet — the board dir doesn't exist)**

```bash
git add CMakeLists.txt
git commit -m "build: point EXTRA_COMPONENT_DIRS at boards/board_cyd_2432s028"
```

---

## Phase 3 — Iface scaffolding (headers + dsky_layout registry)

After Phase 3 the iface headers exist; nothing implements them yet, build still doesn't link.

### Task 8: Create iface headers + board component skeleton

**Files:**
- Create: `components/display_hal/include/display_panel_iface.h`
- Create: `components/touch_input/include/panel_touch_iface.h`
- Create: `components/touch_input/include/touch_input.h`
- Create: `components/touch_input/CMakeLists.txt`
- Create: `components/led_status/include/led_status_iface.h`
- Create: `components/display_hal/include/dsky_layout.h`
- Create: `boards/board_cyd_2432s028/CMakeLists.txt`
- Create: `boards/board_cyd_2432s028/include/board_pins.h`
- Create: `boards/board_cyd_2432s028/board_init.c`

- [ ] **Step 1: Create display_panel_iface.h**

```c
// components/display_hal/include/display_panel_iface.h
#pragma once
//
// display_panel_iface_t — board-agnostic LCD interface used by display_hal.
// Each board component returns a pointer to a static instance via
// board_get_panel(). The renderer pushes RGB565 strips through draw_rows;
// the panel impl handles byte order and address-window setup.

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int       width;
    int       height;
    bool      swap_bytes;
    esp_err_t (*init)(void);
    esp_err_t (*draw_rows)(int y0, int y1, const uint16_t *pixels);
} display_panel_iface_t;

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Create panel_touch_iface.h + touch_input.h + an empty CMakeLists**

```c
// components/touch_input/include/panel_touch_iface.h
#pragma once
//
// panel_touch_iface_t — board-agnostic touchscreen interface.
// Boards without a touchscreen return NULL from board_get_touch().

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_err_t (*init)(void);
    bool (*poll)(int *x, int *y);   // true if pressed; (x,y) in panel coords
} panel_touch_iface_t;

#ifdef __cplusplus
}
#endif
```

```c
// components/touch_input/include/touch_input.h
#pragma once
//
// touch_input — board-agnostic touchscreen-to-DSKY-key bridge. The board
// provides a panel_touch_iface_t; the active dsky_layout provides the
// hit-test. touch_input owns a low-priority FreeRTOS task that polls
// the touch iface at 50 Hz and posts decoded keys into channel_router.

#include "panel_touch_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*touch_hit_test_fn)(int x, int y);

void touch_input_start(const panel_touch_iface_t *touch, touch_hit_test_fn hit_test);

#ifdef __cplusplus
}
#endif
```

```cmake
# components/touch_input/CMakeLists.txt
idf_component_register(
    SRCS         ""
    INCLUDE_DIRS "include"
)
```

(Empty SRCS now — `cst820.c` and `touch_input.c` land in Task 14.)

- [ ] **Step 3: Create led_status_iface.h**

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

- [ ] **Step 4: Create dsky_layout.h**

```c
// components/display_hal/include/dsky_layout.h
#pragma once
//
// dsky_layout_t — resolution-keyed DSKY renderer. display_hal looks one up
// via dsky_layout_for(panel_w, panel_h) and renders the framebuffer in
// strip_h-row passes.

#include "dsky_state.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int  fb_w;
    int  fb_h;
    int  strip_h;             // divides fb_h evenly
    void (*init_strip)(uint16_t *strip, int y0);
    void (*render_strip)(uint16_t *strip, const dsky_state_t *s, int y0);
    int  (*hit_test)(int x, int y);   // -1 if outside any button; NULL = no touch
} dsky_layout_t;

const dsky_layout_t *dsky_layout_for(int w, int h);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 5: Create boards/board_cyd_2432s028/include/board_pins.h**

```c
// boards/board_cyd_2432s028/include/board_pins.h
#pragma once
//
// Pin map for ESP32-2432S028C ("Cheap Yellow Display", 2.8" capacitive variant).
// Reference: third_party/CYD-reference (witnessmenow/ESP32-Cheap-Yellow-Display).
//
// The "C" capacitive variant moves the LCD backlight from GPIO21 (where the
// "R" resistive variant has it) to GPIO27, which frees GPIO21 for the
// CST820 touch INT line.

#define BOARD_NAME "CYD-2432S028C"

// LCD: ILI9341, 240x320 native; rotated to 320x240 landscape via MADCTL
#define BOARD_LCD_HRES   320
#define BOARD_LCD_VRES   240
#define BOARD_LCD_SCK    14
#define BOARD_LCD_MOSI   13
#define BOARD_LCD_MISO   12      // shared with onboard SD slot
#define BOARD_LCD_CS     15
#define BOARD_LCD_DC      2
#define BOARD_LCD_RST    -1      // tied to EN — no GPIO control
#define BOARD_LCD_BL     27      // active-high backlight

// Touch: CST820 capacitive (I2C, 7-bit addr 0x15)
#define BOARD_TOUCH_SDA  33
#define BOARD_TOUCH_SCL  32
#define BOARD_TOUCH_RST  25
#define BOARD_TOUCH_INT  21

// RGB LED — 3 separate active-low GPIOs
#define BOARD_LED_R       4
#define BOARD_LED_G      16
#define BOARD_LED_B      17

// SD card (unused).
#define BOARD_SD_CS       5

// Boot button (pulled high; pressed = low)
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

- [ ] **Step 6: Create boards/board_cyd_2432s028/board_init.c (stub factories)**

```c
// boards/board_cyd_2432s028/board_init.c
#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "board";

const display_panel_iface_t *board_get_panel(void) { return NULL; }   // Task 11
const panel_touch_iface_t   *board_get_touch(void) { return NULL; }   // Task 15
const led_status_iface_t    *board_get_led(void)   { return NULL; }   // Task 16

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

- [ ] **Step 7: Create boards/board_cyd_2432s028/CMakeLists.txt**

```cmake
# boards/board_cyd_2432s028/CMakeLists.txt
idf_component_register(
    SRCS         "board_init.c"
    INCLUDE_DIRS "include"
    REQUIRES     log esp_driver_gpio display_hal touch_input led_status
)
```

- [ ] **Step 8: Verify the build progresses past the missing-board error**

Run: `idf.py build 2>&1 | tail -40`
Expected: build is much further along; will likely fail with linker errors about missing panel_ili9341 / cst820 references — that's fine. We're past Phase 1's "no board component" failure.

- [ ] **Step 9: Commit**

```bash
git add components/display_hal/include/display_panel_iface.h \
        components/display_hal/include/dsky_layout.h \
        components/touch_input/ \
        components/led_status/include/led_status_iface.h \
        boards/board_cyd_2432s028/
git commit -m "scaffolding: ifaces + board_cyd_2432s028 skeleton with stub factories"
```

---

## Phase 4 — ILI9341 panel driver + display_hal glue

### Task 9: Create the ILI9341 driver (pin-parametrized)

**Files:**
- Create: `components/display_hal/include/ili9341_panel.h`
- Create: `components/display_hal/panel_ili9341.c`
- Modify: `components/display_hal/CMakeLists.txt`

The driver lives in `display_hal/`. It takes pins via init param so the board component owns the pin map, not the driver.

- [ ] **Step 1: Create ili9341_panel.h**

```c
// components/display_hal/include/ili9341_panel.h
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

- [ ] **Step 2: Create panel_ili9341.c**

```c
// components/display_hal/panel_ili9341.c
//
// ILI9341 panel driver for the CYD-2432S028C. 240x320 native, rotated to
// 320x240 landscape via MADCTL. SPI2 host, 40 MHz. Backlight active-high
// on a separate GPIO.
//
// References: ILI9341 datasheet rev1.11, Adafruit_ILI9341 library, the
// TFT_eSPI driver settings used by the CYD reference repo.

#include "ili9341_panel.h"

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

#define ILI_SWRESET   0x01
#define ILI_SLPOUT    0x11
#define ILI_DISPON    0x29
#define ILI_CASET     0x2A
#define ILI_PASET     0x2B
#define ILI_RAMWR     0x2C
#define ILI_MADCTL    0x36
#define ILI_PIXFMT    0x3A

// MADCTL: MV=1, MX=0, MY=0, BGR=1 → 320x240 landscape, BGR order
#define MADCTL_LANDSCAPE 0x28

static const char *TAG = "ili9341";

static spi_device_handle_t s_spi;
static ili9341_pins_t      s_pins;

static void cs_low (void) { gpio_set_level(s_pins.cs, 0); }
static void cs_high(void) { gpio_set_level(s_pins.cs, 1); }
static void dc_low (void) { gpio_set_level(s_pins.dc, 0); }
static void dc_high(void) { gpio_set_level(s_pins.dc, 1); }

static void spi_tx(const uint8_t *bytes, size_t len)
{
    if (!len) return;
    spi_transaction_t t = { .length = len * 8, .tx_buffer = bytes };
    cs_low();
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
    cs_high();
}
static void wcmd(uint8_t cmd)                   { dc_low();  spi_tx(&cmd, 1); }
static void wdat(const uint8_t *d, size_t n)    { dc_high(); spi_tx(d, n); }

static void send_init(void)
{
    wcmd(ILI_SWRESET); vTaskDelay(pdMS_TO_TICKS(120));
    wcmd(ILI_SLPOUT);  vTaskDelay(pdMS_TO_TICKS(120));
    wcmd(ILI_PIXFMT);  wdat((uint8_t[]){0x55}, 1);
    wcmd(ILI_MADCTL);  wdat((uint8_t[]){MADCTL_LANDSCAPE}, 1);
    wcmd(ILI_DISPON);  vTaskDelay(pdMS_TO_TICKS(20));
}

static void set_window(int x0, int y0, int x1, int y1)
{
    uint8_t c[4] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
    wcmd(ILI_CASET); wdat(c, 4);
    uint8_t r[4] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };
    wcmd(ILI_PASET); wdat(r, 4);
    wcmd(ILI_RAMWR);
}

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
    ESP_RETURN_ON_ERROR(spi_bus_initialize(ILI_HOST, &buscfg, SPI_DMA_CH_AUTO),
                        TAG, "spi_bus_initialize");

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = ILI_SPI_HZ, .mode = 0, .spics_io_num = -1,
        .queue_size = 4, .flags = SPI_DEVICE_NO_DUMMY,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(ILI_HOST, &devcfg, &s_spi),
                        TAG, "spi_bus_add_device");

    send_init();

    gpio_config_t bl = { .pin_bit_mask = 1ULL << s_pins.bl, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&bl);
    gpio_set_level(s_pins.bl, 1);   // active-high

    ESP_LOGI(TAG, "ILI9341 ready: %dx%d landscape", ILI_W, ILI_H);
    return ESP_OK;
}

esp_err_t ili9341_draw_rows(int y0, int y1, const uint16_t *pixels)
{
    if (y0 < 0 || y1 > ILI_H || y1 <= y0) return ESP_ERR_INVALID_ARG;

    set_window(0, y0, ILI_W - 1, y1 - 1);

    static uint16_t scratch[ILI_W];
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

- [ ] **Step 3: Add panel_ili9341.c to display_hal CMakeLists**

```cmake
# components/display_hal/CMakeLists.txt
idf_component_register(
    SRCS         "display_hal.c" "font5x7.c" "panel_ili9341.c"
    INCLUDE_DIRS "include"
    REQUIRES     channel_router log esp_driver_gpio esp_driver_spi esp_psram
)
```

- [ ] **Step 4: Verify display_hal compiles**

Run: `idf.py build 2>&1 | tail -20`
Expected: progresses further; may still fail at link due to missing dsky_layout / display_hal glue, that's fine.

- [ ] **Step 5: Commit**

```bash
git add components/display_hal/panel_ili9341.c components/display_hal/include/ili9341_panel.h components/display_hal/CMakeLists.txt
git commit -m "display_hal: pin-parametrized ILI9341 driver"
```

---

### Task 10: Add dsky_layout_for() registry

**Files:**
- Create: `components/display_hal/dsky_layout.c`
- Modify: `components/display_hal/CMakeLists.txt`

The registry currently has no entries. The 320×240 layout adds itself in Task 13.

- [ ] **Step 1: Create dsky_layout.c**

```c
// components/display_hal/dsky_layout.c
//
// Resolution → renderer lookup. Add new layouts here.

#include "dsky_layout.h"
#include <stddef.h>

extern const dsky_layout_t dsky_layout_320x240;

const dsky_layout_t *dsky_layout_for(int w, int h)
{
    if (w == 320 && h == 240) return &dsky_layout_320x240;
    return NULL;
}
```

- [ ] **Step 2: Add to CMakeLists**

```cmake
# components/display_hal/CMakeLists.txt
idf_component_register(
    SRCS         "display_hal.c" "font5x7.c" "panel_ili9341.c" "dsky_layout.c"
    INCLUDE_DIRS "include"
    REQUIRES     channel_router log esp_driver_gpio esp_driver_spi esp_psram
)
```

- [ ] **Step 3: Commit (no build verification — extern symbol unresolved until Task 13)**

```bash
git add components/display_hal/dsky_layout.c components/display_hal/CMakeLists.txt
git commit -m "display_hal: dsky_layout_for() registry (extern stub)"
```

---

### Task 11: Wire CYD's board_get_panel() to the ILI9341 driver

**Files:**
- Modify: `boards/board_cyd_2432s028/board_init.c`

- [ ] **Step 1: Add the panel iface adapter to board_init.c**

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
    .width      = BOARD_LCD_HRES,
    .height     = BOARD_LCD_VRES,
    .swap_bytes = true,
    .init       = cyd_panel_init,
    .draw_rows  = ili9341_draw_rows,
};

const display_panel_iface_t *board_get_panel(void) { return &s_panel; }
const panel_touch_iface_t   *board_get_touch(void) { return NULL; }   // Task 15
const led_status_iface_t    *board_get_led(void)   { return NULL; }   // Task 16

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

- [ ] **Step 2: Commit**

```bash
git add boards/board_cyd_2432s028/board_init.c
git commit -m "board_cyd: wire ILI9341 panel through display_panel_iface"
```

---

### Task 12: Replace display_hal.c stub with the real strip-based driver

**Files:**
- Modify: `components/display_hal/display_hal.c`

- [ ] **Step 1: Replace display_hal.c**

```c
// components/display_hal/display_hal.c
//
// Driver-agnostic glue. Looks up the panel iface (from the board) and the
// matching DSKY layout (from resolution), allocates a single strip-sized
// scratch buffer, and pushes frames in `strip_h`-row passes.

#include "display_hal.h"
#include "display_panel_iface.h"
#include "dsky_layout.h"

#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

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

    size_t bytes = (size_t)s_panel->width * s_layout->strip_h * sizeof(uint16_t);
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

- [ ] **Step 2: Verify build state — should be one missing symbol away from linking**

Run: `idf.py build 2>&1 | tail -20`
Expected: still fails to link, missing `dsky_layout_320x240`. Task 13 supplies it.

- [ ] **Step 3: Commit**

```bash
git add components/display_hal/display_hal.c
git commit -m "display_hal: real strip-based driver + iface + layout glue"
```

---

## Phase 5 — DSKY layout + touch + LED + final integration

### Task 13: Layer-1 host test for keypad hit-test

**Files:**
- Create: `components/display_hal/include/dsky_keypad_320x240.h`
- Create: `tests/host/test_keypad_hit.c`
- Modify: `tests/host/Makefile`

The hit-test goes in its own `.c` file (separate from the renderer) so the host test links only that small file — no font, no RGB565 rendering. Task 14 implements `dsky_keypad_320x240.c`; this task writes the failing test against its planned API.

- [ ] **Step 1: Create the keypad geometry header**

```c
// components/display_hal/include/dsky_keypad_320x240.h
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
```

- [ ] **Step 2: Read tests/host/Makefile to see the existing test rule pattern**

Run: `cat tests/host/Makefile`

- [ ] **Step 3: Write the failing test**

```c
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
```

- [ ] **Step 4: Add the test to tests/host/Makefile**

Adapt to the existing pattern — likely add `test_keypad_hit` to the list of binaries and a build rule:

```makefile
test_keypad_hit: test_keypad_hit.c ../../components/display_hal/dsky_keypad_320x240.c
	$(CC) $(CFLAGS) \
	    -I../../components/display_hal/include \
	    -I../../components/channel_router/include \
	    test_keypad_hit.c ../../components/display_hal/dsky_keypad_320x240.c \
	    -o $@
```

Add `test_keypad_hit` to whatever list the Makefile uses to drive `make run` (look for the existing test names).

- [ ] **Step 5: Run; should fail to build**

Run: `cd tests/host && make run`
Expected: `test_keypad_hit` fails because `dsky_keypad_320x240.c` doesn't exist.

- [ ] **Step 6: Don't commit yet — Task 14 makes it pass.**

---

### Task 14: Implement dsky_keypad_320x240.c + dsky_render_320x240.c

**Files:**
- Create: `components/display_hal/dsky_keypad_320x240.c`
- Create: `components/display_hal/dsky_render_320x240.c`
- Modify: `components/display_hal/CMakeLists.txt`

- [ ] **Step 1: Create dsky_keypad_320x240.c**

```c
// components/display_hal/dsky_keypad_320x240.c
//
// Keypad cell table + hit-test for the 320x240 DSKY layout. Pure logic,
// host-testable. The renderer in dsky_render_320x240.c iterates the same
// table to draw cells; this file owns the (col, row) → key mapping.

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

- [ ] **Step 2: Run the host tests; the keypad test should pass**

Run: `cd tests/host && make run`
Expected: `test_keypad_hit OK` plus the existing 3 tests → `ALL PASS`.

- [ ] **Step 3: Create dsky_render_320x240.c**

```c
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
```

- [ ] **Step 4: Add new files to display_hal CMakeLists**

```cmake
# components/display_hal/CMakeLists.txt
idf_component_register(
    SRCS         "display_hal.c" "font5x7.c" "panel_ili9341.c" "dsky_layout.c"
                 "dsky_keypad_320x240.c" "dsky_render_320x240.c"
    INCLUDE_DIRS "include"
    REQUIRES     channel_router log esp_driver_gpio esp_driver_spi esp_psram
)
```

- [ ] **Step 5: Verify build links**

Run: `idf.py build 2>&1 | tail -10`
Expected: build succeeds (touch + LED still NULL on the board, so no touch task spawns yet — but it should boot, splash, and render registers).

- [ ] **Step 6: Commit**

```bash
git add components/display_hal/dsky_keypad_320x240.c \
        components/display_hal/dsky_render_320x240.c \
        components/display_hal/include/dsky_keypad_320x240.h \
        components/display_hal/CMakeLists.txt \
        tests/host/test_keypad_hit.c tests/host/Makefile
git commit -m "display_hal: 320x240 DSKY layout + keypad hit-test (host test)"
```

---

### Task 15: CST820 driver + touch_input task + board wiring

**Files:**
- Create: `components/touch_input/include/cst820.h`
- Create: `components/touch_input/cst820.c`
- Create: `components/touch_input/touch_input.c`
- Create: `components/touch_input/Kconfig.projbuild`
- Modify: `components/touch_input/CMakeLists.txt`
- Modify: `boards/board_cyd_2432s028/board_init.c`
- Modify: `main/app_main.c`

- [ ] **Step 1: Create cst820.h**

```c
// components/touch_input/include/cst820.h
#pragma once
#include <stdbool.h>
#include "esp_err.h"

typedef struct { int sda, scl, rst, intr; } cst820_pins_t;

esp_err_t cst820_init_with_pins(const cst820_pins_t *p);
bool      cst820_poll(int *x_out, int *y_out);
```

- [ ] **Step 2: Create cst820.c**

```c
// components/touch_input/cst820.c
//
// CST820 capacitive touch driver for the CYD-2432S028C. I2C, 7-bit addr 0x15.
// Reports a single (x, y) point after a small reset pulse on the RST pin.
//
// Register 0x01: gesture/finger count (non-zero = touch). Registers 0x03..0x06:
// X/Y as two big-endian 12-bit values in native portrait orientation.

#include "cst820.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "cst820";

#define CST820_ADDR 0x15

static cst820_pins_t           s_pins;
static i2c_master_dev_handle_t s_dev;
static i2c_master_bus_handle_t s_bus;

esp_err_t cst820_init_with_pins(const cst820_pins_t *p)
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

bool cst820_poll(int *x_out, int *y_out)
{
    uint8_t buf[6] = { 0 };
    if (cst820_read(0x01, buf, 6) != ESP_OK) return false;
    if (buf[0] == 0) return false;

    int x = ((buf[2] & 0x0F) << 8) | buf[3];
    int y = ((buf[4] & 0x0F) << 8) | buf[5];
    // Native CST820 reports 240(x) × 320(y) portrait. Rotate to landscape
    // so coordinates match the panel's MADCTL orientation:
    //   landscape_x = native_y, landscape_y = 240 - native_x
    *x_out = y;
    *y_out = 240 - x;
    return true;
}
```

- [ ] **Step 3: Create touch_input.c**

```c
// components/touch_input/touch_input.c
//
// 50 Hz touch poll task with edge-trigger + 80 ms debounce. On a
// transition from "released" to "pressed", looks up the cell at (x, y)
// via the layout's hit_test and posts the decoded key into channel_router.

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
                ESP_LOGI(TAG, "tap (%d,%d) -> key %d", x, y, code);
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

- [ ] **Step 4: Create Kconfig for touch driver choice**

```
# components/touch_input/Kconfig.projbuild
menu "espAGC touch driver"

choice ESPAGC_CYD_TOUCH
    prompt "CYD touch controller"
    default ESPAGC_CYD_TOUCH_CST820
    help
        Selects the touch driver for ESP32-2432S028 variants.
        Only CST820 is implemented in v1; GT911/XPT2046 are reserved
        for follow-up work.

config ESPAGC_CYD_TOUCH_CST820
    bool "CST820 (capacitive — 2432S028C)"

config ESPAGC_CYD_TOUCH_GT911
    bool "GT911 (capacitive — alternate)"

config ESPAGC_CYD_TOUCH_XPT2046
    bool "XPT2046 (resistive — 2432S028R)"

endchoice

endmenu
```

- [ ] **Step 5: Update touch_input/CMakeLists.txt**

```cmake
# components/touch_input/CMakeLists.txt
idf_component_register(
    SRCS         "touch_input.c" "cst820.c"
    INCLUDE_DIRS "include"
    REQUIRES     channel_router log esp_driver_gpio esp_driver_i2c esp_timer freertos
)
```

- [ ] **Step 6: Wire CYD's board_get_touch() to CST820**

In `boards/board_cyd_2432s028/board_init.c`, add to the includes:
```c
#include "cst820.h"
```

After the existing `s_panel` block, add:
```c
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
```

Replace `const panel_touch_iface_t *board_get_touch(void) { return NULL; }` with:
```c
const panel_touch_iface_t *board_get_touch(void) { return &s_touch; }
```

- [ ] **Step 7: Wire app_main to start touch_input when board provides one**

In `main/app_main.c`, add to the includes block:
```c
#include "display_panel_iface.h"
#include "touch_input.h"
#include "dsky_layout.h"
```

Add forward declarations near the top, just below the existing `static const char *TAG = "app";`:
```c
extern const display_panel_iface_t *board_get_panel(void);
extern const panel_touch_iface_t   *board_get_touch(void);
```

Set `enable_usb_cdc = false` in the dsky_input config (CYD has no USB-Serial-JTAG):
```c
    dsky_input_config_t in_cfg = {
        .enable_usb_cdc = false,    // CYD has CP2102 USB-UART, no USB-Serial-JTAG
        .enable_wifi_ap = true,
        .wifi_ssid = "espAGC",
        .wifi_password = "",
    };
    dsky_input_start(&in_cfg);
```

After `dsky_input_start(&in_cfg);`, add:
```c
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

Update `main/CMakeLists.txt` REQUIRES to include `touch_input` and `display_hal` if either is missing — read it first to be sure:

Run: `cat main/CMakeLists.txt`

Edit if necessary.

- [ ] **Step 8: Verify build**

Run: `idf.py build 2>&1 | tail -10`
Expected: build succeeds.

- [ ] **Step 9: Verify host tests still pass**

```bash
cd tests/host && make run
```
Expected: `ALL PASS` (4/4 including `test_keypad_hit`).

- [ ] **Step 10: Commit**

```bash
git add components/touch_input/ \
        boards/board_cyd_2432s028/board_init.c \
        main/app_main.c main/CMakeLists.txt
git commit -m "touch_input: CST820 driver + 50 Hz poll task; wire into app_main"
```

---

### Task 16: RGB GPIO LED + boot heartbeat

**Files:**
- Create: `components/led_status/include/rgb_gpio.h`
- Create: `components/led_status/rgb_gpio.c`
- Modify: `components/led_status/CMakeLists.txt`
- Modify: `boards/board_cyd_2432s028/board_init.c`
- Modify: `main/app_main.c`

- [ ] **Step 1: Create rgb_gpio.h**

```c
// components/led_status/include/rgb_gpio.h
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct { int r, g, b; bool active_low; } rgb_gpio_pins_t;

void rgb_gpio_init_with_pins(const rgb_gpio_pins_t *p);
void rgb_gpio_set_rgb(uint8_t r, uint8_t g, uint8_t b);
```

- [ ] **Step 2: Create rgb_gpio.c**

```c
// components/led_status/rgb_gpio.c
//
// 3-GPIO RGB LED driver. Each color channel toggles on/off at 0x80 — no
// PWM in v1. Used by the CYD's onboard RGB LED, which is wired as 3
// separate active-low GPIOs (4/16/17).

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

- [ ] **Step 3: Update led_status/CMakeLists.txt**

```cmake
# components/led_status/CMakeLists.txt
idf_component_register(
    SRCS         "rgb_gpio.c"
    INCLUDE_DIRS "include"
    REQUIRES     log esp_driver_gpio
)
```

- [ ] **Step 4: Wire CYD's board_get_led() to rgb_gpio**

In `boards/board_cyd_2432s028/board_init.c`, add to the includes:
```c
#include "rgb_gpio.h"
```

After the touch iface block, add:
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
```

Replace `const led_status_iface_t *board_get_led(void) { return NULL; }` with:
```c
const led_status_iface_t *board_get_led(void) { return &s_led; }
```

- [ ] **Step 5: Wire app_main heartbeat**

In `main/app_main.c`, add to the includes:
```c
#include "led_status_iface.h"
```

Add forward declaration with the others:
```c
extern const led_status_iface_t *board_get_led(void);
```

In `app_main()`, between `board_init();` and `channel_router_init();`, add:
```c
    const led_status_iface_t *led = board_get_led();
    if (led) {
        led->init();
        led->set_rgb(0xFF, 0x80, 0x00);   // amber while booting
    }
```

After the final `ESP_LOGI(TAG, "espAGC running");`, add:
```c
    if (led) led->set_rgb(0x00, 0x40, 0x00);   // dim green = healthy
```

- [ ] **Step 6: Verify build**

Run: `idf.py build 2>&1 | tail -10`
Expected: build succeeds.

- [ ] **Step 7: Verify host tests still pass**

```bash
cd tests/host && make run
```
Expected: `ALL PASS` (4/4).

- [ ] **Step 8: Commit**

```bash
git add components/led_status/ \
        boards/board_cyd_2432s028/board_init.c \
        main/app_main.c
git commit -m "led_status: 3-GPIO RGB driver + boot heartbeat (amber → dim green)"
```

---

### Task 17: README rewrite for CYD-only

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Read current README.md to understand its structure**

Run: `cat README.md`

- [ ] **Step 2: Rewrite the README**

Edits:

1. **Title** — replace first line:
   ```markdown
   # espAGC — Apollo Guidance Computer on the Cheap Yellow Display (ESP32-2432S028C)
   ```

2. **Intro paragraph** — replace the LilyGO description with:
   > A self-contained Apollo Guidance Computer running on the ESP32-WROOM-32 inside an [ESP32-2432S028C "Cheap Yellow Display"](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display). The board becomes a self-contained DSKY — controlled from a 320×240 on-screen 19-key touch keypad, *and* the existing WiFi web UI. Both **Luminary 099 (LM)** and **Comanche 055 (CSM)** mission ROMs are assembled at build time from the original AGC sources in [virtualagc/virtualagc](https://github.com/virtualagc/virtualagc) via yaYUL, and embedded directly in the firmware.

3. **Status table** — replace Layer 3 row with:
   ```markdown
   | Layer 3 — hardware | **boots, runs** | Firmware brings up the ILI9341 panel, loads Luminary099, joins WiFi (or falls back to a SoftAP `espAGC`), and accepts taps on the on-screen keypad. |
   ```

   And update Layer 2's "No QEMU machine for ESP32-C5" note — drop the C5 reference; ESP32 *is* supported by Espressif's QEMU fork, so this row could change. For now keep "**deferred**" with a shorter note: "QEMU integration deferred."

4. **Layout** section — replace the tree with the post-port shape (mirror the spec's component-layout block).

5. **Build & flash** — replace with:
   ````markdown
   Requires **ESP-IDF v6.0+** and a host C compiler (MinGW-w64 on Windows; gcc/clang on Linux/macOS) for yaYUL.

   ```powershell
   git clone --recurse-submodules https://github.com/zombodotcom/espAGC.git
   cd espAGC

   . C:\esp\v6.0.1\esp-idf\export.ps1
   idf.py set-target esp32
   idf.py build       # ~2 min cold (host yaYUL build + LM/CSM assembly)
   idf.py -p COM<n> flash monitor
   ```

   On boot you'll see the splash, then the DSKY registers update live. Hold the BOOT button at reset to switch ROM to Comanche055.
   ````

6. **DSKY input** — replace the USB-Serial-JTAG bullet with:
   > - **Touchscreen**: tap the on-screen 19-key keypad. Same key set as the WiFi web UI.

7. **Dependencies** — drop the LVGL line ("currently unused; will return…"). Keep ESP-IDF v6.0+ and host gcc.

8. Anywhere else the README mentions "T-Dongle-C5", "ST7735", "APA102", "USB-Serial-JTAG" — replace or remove.

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "README: rewrite for CYD-2432S028C single target"
```

---

## Self-review checklist (run after all tasks complete)

- [ ] Layer 1 host tests pass: `cd tests/host && make run` → 4/4 PASS including `test_keypad_hit`.
- [ ] Build succeeds: `idf.py set-target esp32 && idf.py build`.
- [ ] No grep hits for `T-Dongle`, `ST7735`, `APA102`, `tdongle_c5` in source files (`git grep -nE "T-Dongle|st7735|apa102|tdongle_c5" -- ':!docs/' ':!third_party/' ':!.claude/'`).
- [ ] No `#ifdef IDF_TARGET_*` / `#ifdef BOARD_*` in shared code.
- [ ] On-device check (if hardware available): boot splash visible, registers update, touch keypad emits keys, WiFi joins or SoftAP appears.
