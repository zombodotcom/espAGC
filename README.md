# espAGC — Apollo Guidance Computer on the LilyGO T-Dongle-C5

A self-contained Apollo Guidance Computer running on the ESP32-C5HR8 inside a
[LilyGO T-Dongle-C5](https://github.com/Xinyuan-LilyGO/T-Dongle-C5).
The dongle becomes a self-contained DSKY — controllable from USB-Serial-JTAG
*and* a built-in WiFi web UI. Both **Luminary 099 (LM)** and **Comanche 055
(CSM)** mission ROMs are assembled at build time from the original AGC sources
in [virtualagc/virtualagc](https://github.com/virtualagc/virtualagc) via
yaYUL, and embedded directly in the firmware.

The emulator core is yaAGC. License is **GPL v2** (carries through from yaAGC).

## Status

| Layer | Status | Notes |
|---|---|---|
| Layer 1 — host tests (`tests/host/`) | **3/3 PASS** | ROM loader, engine boot, channel-10 DSKY emit |
| Layer 2 — QEMU | **deferred** | No QEMU machine for ESP32-C5 in mainline or [Espressif's QEMU fork](https://github.com/espressif/qemu) (latest `esp-develop-9.2.2-20260417` covers ESP32 / C3 / S3 only). A parallel S3 build is on the roadmap to enable Layer 2. |
| Layer 3 — hardware | **boots, runs** | Firmware brings up PSRAM, loads Luminary099, opens an `espAGC` SoftAP at `http://192.168.4.1/`, and accepts USB-Serial-JTAG keypresses. |

DSKY output currently logs to UART. The on-panel **ST7735 + LVGL DSKY UI is
queued** — the maintained `waveshare/esp_lcd_st7735` managed component is
broken on this exact panel (wrong INVON/INVOFF, wrong reset timing, wrong
MADCTL); the right path is to port a known-good direct driver.

## Layout

```
components/
  agc_core/          yaAGC engine wrapper. Cherry-picks engine sources
                     from third_party/virtualagc/yaAGC, replaces
                     SocketAPI.c with io_callbacks.c and agc_engine_init.c
                     with a memory-loading agc_init.c.
  apollo_rom/        Runs host yaYUL on virtualagc's Luminary099 / Comanche055
                     mission trees at configure time, embeds the binaries
                     via EMBED_FILES.
  channel_router/    AGC IO channels <-> a dsky_state_t snapshot, with a
                     lock-free input ringbuffer for keystrokes.
  display_hal/       Pluggable display backend. Default = console
                     (logs DSKY snapshot over UART). Reserved seat
                     for a real LVGL+ST7735 panel impl.
  dsky_input/        USB-CDC (USB-Serial-JTAG) and WiFi (HTTP POST /key)
                     transports, both feeding channel_router.
  led_status/        APA102 RGB LED status indicator.
  board_tdongle_c5/  Pin map + minimal board init.
main/                app_main.c — boot sequence + task spawn.
tests/host/          Layer 1 host gcc tests. ~1 s to run.
tools/
  build_yayul.cmake     ExternalProject for host yaYUL.
  yayul_host_project/   Top-level CMakeLists wrapper for yaYUL.
  assemble_rom.cmake    Custom command runner: yaYUL MAIN.agc -> bank-ordered .bin.
third_party/         Submodules: virtualagc, Apollo-11, T-Dongle-C5.
```

## Build & flash

Requires **ESP-IDF v6.0+** (ESP32-C5 support) and a host C compiler (MinGW-w64
on Windows; gcc/clang on Linux/macOS) for yaYUL.

```powershell
git clone --recurse-submodules https://github.com/zombodotcom/espAGC.git
cd espAGC

# activate IDF (path may differ)
. C:\esp\v6.0.1\esp-idf\export.ps1

idf.py set-target esp32c5
idf.py build       # ~2 min cold (host yaYUL build + LM/CSM assembly)
idf.py -p COM<n> flash monitor
```

On boot you'll see something like:

```
I (1507) app: loading ROM Luminary099 (73728 bytes)
I (1507) usb_cdc: USB-Serial-JTAG DSKY input ready
I (2227) wifi_input: WiFi AP 'espAGC' up; web DSKY at http://192.168.4.1/
```

Hold the boot button at reset to switch ROM to Comanche055.

### DSKY input

- **USB-Serial-JTAG**: type characters into `idf.py monitor` or any serial
  terminal. Single-character keys: digits `0`-`9`, `+`, `-`, and the
  shortcuts `V` (VERB), `N` (NOUN), `E` (ENTR), `C` (CLR), `P` (PRO),
  `R` (RSET), `K` (KEYREL).
- **WiFi web UI**: connect to the open AP `espAGC`, browse to
  `http://192.168.4.1/`. SPA has a 19-button DSKY keypad and physical-keyboard
  shortcuts.

## Layer 1 host tests

```powershell
cd tests\host
mingw32-make run    # gcc must be on PATH
```

Three tests, each PASS in well under a second:

| Test | What it asserts |
|---|---|
| `test_rom_load` | ROM bank-reorder loader produces the right Fixed[bank][word] layout |
| `test_engine_boot` | 50 000 AGC cycles run cleanly; PC moves off boot vector |
| `test_channel10_emit` | Engine emits writes to ch010 (DSKY display) + ch011 (status) within 200 000 cycles |

The Makefile compiles `agc_engine.c` straight from the virtualagc submodule
plus our `agc_init.c` and a host-side IO stub — no ESP-IDF, no FreeRTOS
involvement.

## Dependencies

- ESP-IDF v6.0+
- MinGW-w64 / gcc / clang on the host (for yaYUL)
- LVGL + esp_lvgl_port — currently unused; will return when the ST7735 driver
  is ported in.

## License

**GPL v2.** yaAGC's license carries through. See `LICENSE` and the
`COPYING` file inside `third_party/virtualagc/`.
