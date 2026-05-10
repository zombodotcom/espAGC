# espAGC — Apollo Guidance Computer on the Cheap Yellow Display (ESP32-2432S028)

A self-contained Apollo Guidance Computer running on the ESP32-WROOM-32 inside an [ESP32-2432S028 "Cheap Yellow Display"](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) (the canonical 2.8" CYD with resistive XPT2046 touch). The board becomes a self-contained DSKY — controlled from a 320×240 on-screen 19-key touch keypad, *and* the existing WiFi web UI. Both **Luminary 099 (LM)** and **Comanche 055 (CSM)** mission ROMs are assembled at build time from the original AGC sources in [virtualagc/virtualagc](https://github.com/virtualagc/virtualagc) via yaYUL, and embedded directly in the firmware.

The emulator core is yaAGC. License is **GPL v2** (carries through from yaAGC).

## Status

| Layer | Status | Notes |
|---|---|---|
| Layer 1 — host tests (`tests/host/`) | **4/4 PASS** | ROM loader, engine boot, channel-10 DSKY emit, keypad hit-test |
| Layer 2 — QEMU | **deferred** | QEMU integration deferred. |
| Layer 3 — hardware | **boots, runs** | Firmware brings up the ILI9341 panel, loads Luminary099, joins WiFi (or falls back to a SoftAP `espAGC`), and accepts taps on the on-screen 19-key keypad. |

DSKY output renders as a 320×240 framebuffer on the ILI9341 panel — status panel, register window, and an on-screen 19-key keypad backed by the XPT2046 resistive touchscreen. No LVGL — direct framebuffer in 80-row strips, three passes per frame.

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
  display_hal/       320x240 DSKY renderer. ILI9341 panel driver,
                     dsky_layout_320x240, framebuffer rendered in
                     three 80-row strips.
  touch_input/       XPT2046 resistive driver + 50 Hz poll task that
                     posts decoded keys via channel_router_post_key.
  dsky_input/        WiFi (HTTP POST /key) transport feeding channel_router.
  led_status/        3-GPIO RGB LED driver (active-low) for the CYD's
                     onboard status LED.
boards/
  board_cyd_2432s028/  Pin map + factory functions returning panel,
                       touch, and LED ifaces.
main/                app_main.c — boot sequence + task spawn.
tests/host/          Layer 1 host gcc tests. ~1 s to run.
tools/
  build_yayul.cmake     ExternalProject for host yaYUL.
  yayul_host_project/   Top-level CMakeLists wrapper for yaYUL.
  assemble_rom.cmake    Custom command runner: yaYUL MAIN.agc -> bank-ordered .bin.
third_party/         Submodules: virtualagc, Apollo-11, CYD-reference.
```

## Build & flash

Requires **ESP-IDF v6.0+** and a host C compiler (MinGW-w64 on Windows; gcc/clang on Linux/macOS) for yaYUL.

```powershell
git clone --recurse-submodules https://github.com/zombodotcom/espAGC.git
cd espAGC

# activate IDF (path may differ)
. C:\esp\v6.0.1\esp-idf\export.ps1

idf.py set-target esp32
idf.py build       # ~2 min cold (host yaYUL build + LM/CSM assembly)
idf.py -p COM<n> flash monitor
```

On boot you'll see something like:

```
I (1507) app: loading ROM Luminary099 (73728 bytes)
I (2227) wifi_input: WiFi AP 'espAGC' up; web DSKY at http://192.168.4.1/
I (2300) ili9341: ILI9341 ready: 320x240 landscape
I (2301) dsky:    display_hal up: 320x240, strip_h=80
I (2305) xpt2046: XPT2046 ready (sck=25 mosi=32 miso=39 cs=33 irq=36)
I (2306) touch:   touch_input task up
```

Hold the boot button at reset to switch ROM to Comanche055.

### DSKY input

- **Touchscreen**: tap the on-screen 19-key keypad. Same key set as the WiFi web UI.
- **WiFi web UI**: connect to the network configured in `idf.py menuconfig` → espAGC WiFi (or to the open AP `espAGC` if no SSID is set), browse to `http://<dongle-ip>/` (or `http://192.168.4.1/` in SoftAP mode). SPA has a 19-button DSKY keypad and physical-keyboard shortcuts.

## Layer 1 host tests

```powershell
cd tests\host
mingw32-make run    # gcc must be on PATH
```

Four tests, each PASS in well under a second:

| Test | What it asserts |
|---|---|
| `test_rom_load` | ROM bank-reorder loader produces the right Fixed[bank][word] layout |
| `test_engine_boot` | 50 000 AGC cycles run cleanly; PC moves off boot vector |
| `test_channel10_emit` | Engine emits writes to ch010 (DSKY display) + ch011 (status) within 200 000 cycles |
| `test_keypad_hit` | 320×240 keypad cell centers map to the right `dsky_key_t` codes; out-of-bounds returns -1 |

The Makefile compiles `agc_engine.c` straight from the virtualagc submodule
plus our `agc_init.c` and a host-side IO stub — no ESP-IDF, no FreeRTOS
involvement.

## Dependencies

- ESP-IDF v6.0+
- MinGW-w64 / gcc / clang on the host (for yaYUL)

## License

**GPL v2.** yaAGC's license carries through. See `LICENSE` and the
`COPYING` file inside `third_party/virtualagc/`.
