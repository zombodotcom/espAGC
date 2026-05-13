# espAGC — Apollo Guidance Computer on the Cheap Yellow Display (ESP32-2432S028)

A self-contained Apollo Guidance Computer running on the ESP32-WROOM-32 inside an [ESP32-2432S028 "Cheap Yellow Display"](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) (the canonical 2.8" CYD with resistive XPT2046 touch). The board becomes a self-contained DSKY — controlled from a 320×240 on-screen 19-key touch keypad, *and* the existing WiFi web UI. Both **Luminary 099 (LM)** and **Comanche 055 (CSM)** mission ROMs are assembled at build time from the original AGC sources in [virtualagc/virtualagc](https://github.com/virtualagc/virtualagc) via yaYUL, and embedded directly in the firmware.

The emulator core is yaAGC. License is **GPL v2** (carries through from yaAGC).

## Status

| Layer | Status | Notes |
|---|---|---|
| Layer 1 — pure-logic host tests | **4/4 PASS** | ROM loader, engine boot, channel-10 DSKY emit, keypad hit-test |
| Layer 2a — engine + real channel_router | **12/12 PASS** | Boot alarm dump, P00 select, lamp test, RSET clears, Apollo 11 transcript replay, auto-RSET, IMODES, FAILREG, idle quiet, executive state, CHARIN dispatch |
| Layer 2b — renderer pixel tests | **2/2 PASS** | Blank frame FNV-1a hash, region assertions for lit PROG/VERB/NOUN cells |
| Layer 2c — canonical yaAGC socket port | **5/5 PASS** ✅ | `test_yaagc_socket_host` + `yaagc_socket_reliability.py` drives our build via canonical 4-byte protocol over TCP and reaches PRG=00 (ch010=55265) on V36E V37E 00E V37E 00E — matches yaAGC.exe baseline exactly. `test_yaagc_socket_local` does the same via the synthetic-client inject path (no TCP) and also passes. |
| Layer 3 — QEMU | **available** | Espressif's `qemu-xtensa` (9.2.2) supports `-machine esp32`. `idf.py qemu monitor` runs the firmware. WiFi radio not emulated; everything else (engine, peripheral_stub, channel_router, display_hal) runs. |
| Layer 4 — hardware | **boots, engine runs, V37 programs transition correctly** ✅ | With `CONFIG_AGC_YAAGC_SOCKET=y`: cold boot → WiFi associates → deferred LM_INI fires via canonical mask+value flow. V37E63E reaches **`active=p044322@021301`** (P63 SERVICER scheduled and running). V05N09 displays alarms. The Apollo 11 landing transcript sequence walks PDI → 1201/1202 PRO acks → V16N68 → P66 → touchdown. PROG/VERB/NOUN render on both LCD and web DSKY. P63's R1/R2/R3 stay blank until PIPA/LR pulse injection lands (see "Descent thrust" below — foundation in place, full landing simulation is multi-session work). |

### What changed (2026-05-13)

The hardware-display blank was a **driver-loop bug**, not a Luminary issue. Every in-process port of yaAGC's main loop we tried failed V37E00E×2 with the same deterministic state, but yaAGC.exe + Python socket driver passed 5/5. Bisection ruled out our integration features, our SimExecute port, our pacing, and per-channel value differences in LM_INI.

The fix is a port of canonical `SocketAPI.c` + `agc_utilities.c` into `components/yaagc_socket/`, gated by `CONFIG_AGC_YAAGC_SOCKET` (default off). When on:

- ESP32 listens on TCP `:19850` speaking the canonical 4-byte protocol.
- `io_callbacks.c::ChannelInput/Output/Routine` forward to the socket layer.
- LCD / touch / web keypresses route through `channel_router_post_key → yaagc_socket_inject_key → canonical drain` (synthetic local client; slot 0 reserved).
- `peripheral_stub_init` fires LM_INI through `yaagc_socket_inject_packet` with canonical masks (`077777` / `077776` / `000174`) — same byte stream the Python driver sends to yaAGC.exe.

Flip on with `idf.py menuconfig` → "espAGC canonical yaAGC socket (Task #18)" → enable. Default-off keeps the legacy channel_router path unchanged. Existing host Layer-2 tests still pass.

DSKY output renders as a 320×240 framebuffer on the ST7789 panel — status panel, register window, and an on-screen 19-key keypad backed by the XPT2046 resistive touchscreen. No LVGL — direct framebuffer in 80-row strips, three passes per frame.

## How V35E works — the cold-boot recovery (host build)

Cold-boot Luminary099 on yaAGC has a documented deadlock without a `--resume` core-dump file or a connected LM_Simulator socket peer. 1/ACCSET (PRIO=27110, allocated by DAPIDLER's first T5RUPT) executes interpretive code that gets caught in `INTERPRETER.agc:681` GOTO indirection — POLISH=0 dereferencing zero scratch storage indefinitely. Block II AGC is non-preemptive, so CHARIN (allocated on keypress) can never take CPU. Vanilla yaAGC has the same problem (verified via `yaagc_ref` and `test_ref_v37_slots` — see [HANDOFF.md](HANDOFF.md)).

`peripheral_stub_tick` (in `components/peripheral_stub/peripheral_stub.c`) provides multi-stage recovery on the **host build**:

1. **GOJAM-rescue.** When NEWJOB stays the same across consecutive ticks (executive wants to swap but can't), trigger a simulated GOJAM matching `agc_engine.c:2246-2298`.

2. **CHARIN dispatch injection.** Each tick, scan the 7 inactive job slots for one matching CHARIN's signature (PRIO=30110, LOC=02077, BANKSET=060101). If found and the active slot is the stuck 1/ACCSET (027110), manually copy the CHARIN slot's state into the active set (software CHANG2), and set RegZ/FBANK/EBANK/SBANK so the engine starts executing CHARIN's first instruction.

3. **Stuck-Z rescue.** When the active job pins Z within a 16-address window for 4+ ChannelRoutine ticks, force GOJAM. Generic catch-all for interpretive-loop deadlocks.

4. **WAKESTAL rescue.** When a slot parks at INTSTALL's CADR (027414/027415) for 4+ ticks, force GOJAM. Handles V37's INTSTALL JOBSLEEP path.

Result on host: V35 LAMP TEST runs the full sequence — `make run` → ALL PASS confirms via `test_v35e_full`.

**ESP32 hardware status: rescues fire but display still stays blank.** Cold-boot trips NW alarm (FAILREG[0]=01107), engine cycles between stuck states until alarms settle and slot 0 ends up empty. See [HANDOFF.md](HANDOFF.md) for current debugging and next steps. The dual-core layout (agc_task on APP_CPU, ui_task on PRO_CPU) is in place but the display work isn't done yet.

## Boot behavior — the PROG ALARM caveat

A fresh Luminary 099 boot leaves **PROG ALARM**, **RESTART**, and the **NightWatchman** watchdog all asserted. This is canonical Block II AGC fresh-start behavior — the astronaut is supposed to acknowledge with **RSET**.

After RSET:
- **RESTART clears.** The hardware-direct flip-flop in `agc_engine.c` (`State->RestartLight = 0` when ch015 is written with keycode 022) fires correctly.
- **PROG ALARM stays lit.** Luminary's RSET handler (`PINBALL_GAME__BUTTONS_AND_LIGHTS.agc::ERROR`) clears `DSPTAB +11D` *and* the fault bits in `IMODES30`/`IMODES33` — but the comment right there reads *"IF THE FAILURE STILL EXISTS, THE ALARM WILL COME BACK."* Luminary still sees missing peripherals (no IMU CDU counters, no radar data, no AOT marks), re-asserts the fault bits, and re-asserts PROG ALARM.

Solving this end-to-end requires a peripheral stub task — see `docs/SESSION_NOTES.md` for the current investigation and what's next.

## Layout

```
components/
  agc_core/          yaAGC engine wrapper. Cherry-picks engine sources
                     from third_party/virtualagc/yaAGC, replaces
                     SocketAPI.c with io_callbacks.c and agc_engine_init.c
                     with a memory-loading agc_init.c. Initializes ch030
                     to 037777 (matches upstream yaAGC default — all
                     signals de-asserted, "IMU not yet operating").
  apollo_rom/        Runs host yaYUL on virtualagc's Luminary099 / Comanche055
                     mission trees at configure time, embeds the binaries
                     via EMBED_FILES.
  channel_router/    AGC IO channels <-> a dsky_state_t snapshot, with a
                     lock-free input ringbuffer for keystrokes. Routes
                     ch015 keypresses through agc_engine.c::WriteIO so
                     the RSET-clears-RESTART hardware path fires.
  display_hal/       320x240 DSKY renderer. ST7789 panel driver,
                     dsky_layout_320x240, framebuffer rendered in
                     three 80-row strips.
  touch_input/       XPT2046 resistive driver + 50 Hz poll task that
                     posts decoded keys via channel_router_post_key.
  dsky_input/        WiFi (HTTP POST /key) transport feeding channel_router.
  led_status/        3-GPIO RGB LED driver (active-low) for the CYD's
                     onboard status LED.
  sequences/         Canned DSKY key sequences (lamp test, P00 select,
                     V16N36 time, V05N09, RSET) exposed via the web UI.
boards/
  board_cyd_2432s028/  Pin map + factory functions returning panel,
                       touch, and LED ifaces.
main/                app_main.c — boot sequence + task spawn.
tests/host/          Three-layer host gcc test harness. ~2 s total.
tools/
  build_yayul.cmake     ExternalProject for host yaYUL.
  yayul_host_project/   Top-level CMakeLists wrapper for yaYUL.
  assemble_rom.cmake    Custom command runner: yaYUL MAIN.agc -> bank-ordered .bin.
third_party/         Submodules: virtualagc, Apollo-11, CYD-reference, T-Dongle-C5.
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
I (2300) st7789: ST7789 ready: 320x240 landscape
I (2301) dsky:    display_hal up: 320x240, strip_h=80
I (2305) xpt2046: XPT2046 ready (sck=25 mosi=32 miso=39 cs=33 irq=36)
I (2306) touch:   touch_input task up
```

Hold the boot button at reset to switch ROM to Comanche055.

### DSKY input

- **Touchscreen**: tap the on-screen 19-key keypad. Same key set as the WiFi web UI.
- **WiFi web UI**: connect to the network configured in `idf.py menuconfig` → espAGC WiFi (or to the open AP `espAGC` if no SSID is set), browse to `http://<dongle-ip>/` (or `http://192.168.4.1/` in SoftAP mode). SPA has a **DSKY display mirror** (PROG/VERB/NOUN, R1/R2/R3, all 12 status lamps — exactly the same fields the ST7789 LCD shows, polled from `/state` at 5 Hz), a 19-button DSKY keypad, physical-keyboard shortcuts, and a one-click menu of canned sequences (lamp test, P00 select, RSET, etc.).

### What verbs work — try these first

| Sequence | Type | What you'll see |
|---|---|---|
| **V35E** (lamp test) | host ✅ / hardware ⚠️ | **Host**: `test_v35e_full` shows VRB=35 after digits then [8,8][8,8][8,8] after E (all digits + lamps). **Hardware**: keypress reaches CHARIN but DSKY display stays blank — see [HANDOFF.md](HANDOFF.md) |
| **R** (RSET) | host ✅ | Clears RESTART lamp on host. RestartLight stays lit on hardware due to repeated cold-boot NW alarms |
| **V37E00E** (select P00) | host partial ⚠️ | `verify-ref` exits 2 (PARTIAL OK): channel-value subsets match WSL ground truth but PRG=00 (ch010=55265) doesn't emit. V37's NEWMODE/MMCHANG transition crashes the engine on the second ENTR (slot saves with wrong BANKSET). `test_ref_v37_slots` confirms this is upstream-yaAGC cycle-driven-mode behavior, not our integration. Hardware doesn't reach PRG=00 either. |

V35E is the headline demo — it exercises CHARIN dispatch, lamp test verb 35, ch010 row-by-row digit + lamp output, and the full DSKY render pipeline end-to-end.

### Diagnostic tests (run on host without hardware)

```powershell
cd tests\host
mingw32-make diag                       # build all diagnostic binaries
ROM=../../build/roms/Luminary099.bin ./test_v35e_full.exe         # step-by-step V35E
ROM=../../build/roms/Luminary099.bin ./test_charin_dispatch.exe   # CHARIN slot allocation
ROM=../../build/roms/Luminary099.bin ./test_v37_slots.exe         # V37 sequence + slot dump
ROM=../../build/roms/Luminary099.bin ./yaagc_ref.exe              # vanilla yaAGC baseline
```

#### State-comparison + slot-write tracing (V37E00E investigation)

```powershell
cd tests\host
mingw32-make test_state_compare.exe test_slot_writes.exe

# Capture 25 per-second core dumps of host's V36E V37E 00E V37E 00E run
ROM=../../build/roms/Luminary099.bin DUMPDIR=wsl_dumps/host_v37 ./test_state_compare.exe

# Diff against committed WSL ground-truth at wsl_dumps/ref/
py compare_dumps.py host_v37 ref                              # high-level summary
py parse_core_dump.py wsl_dumps/host_v37/core.018 wsl_dumps/ref/core.019 --diff

# Step the engine one instruction at a time and log every write to slot-0
# (E[0][0164..0167]). Pinpoints exactly which Z address writes the corrupt
# PRIORITY=030401 / LOC=02146 that causes the V37E00E×2 crash to Z=0.
ROM=../../build/roms/Luminary099.bin ./test_slot_writes.exe   # writes slot_writes.log
grep "Z=02751 " slot_writes.log | head                        # the smoking gun
```

Pre-captured WSL reference dumps (`wsl_dumps/ref/core.000..026`) are committed; re-capture via `bash capture_with_dumps.sh` from inside WSL when the upstream behavior needs refreshing. PRG=00 emits in ref `core.022` (`OutputChannel10[11]=55265`); host never reaches that state — see [HANDOFF.md](HANDOFF.md) and `~/.claude/projects/.../memory/project_v37_slot_corruption_diagnosed.md` for the current diagnosis.

### QEMU — emulate the WROOM-32 firmware locally

Iterating on `peripheral_stub.c` / `channel_router.c` / `agc_init.c` without burning the flash → COM7 monitor cycle:

```powershell
. C:\esp\v6.0.1\esp-idf\export.ps1
python $env:IDF_PATH\tools\idf_tools.py install qemu-xtensa     # one-time, ~70MB
. C:\esp\v6.0.1\esp-idf\export.ps1                              # re-source so PATH picks up qemu

# QEMU's ESP32 model doesn't emulate the WiFi PHY — esp_wifi_init() asserts.
# Use the sdkconfig.qemu overlay to disable WiFi for QEMU builds:
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.qemu" build
idf.py qemu                    # boot QEMU, serial → stdout
idf.py qemu monitor            # boot + interactive serial monitor
idf.py qemu --gdb              # wait for `idf.py gdb` to attach (no JTAG)
idf.py qemu --graphics         # adds virtual framebuffer for the ST7789
```

Verified working (2026-05-12) — QEMU boots the firmware, the engine ticks the peripheral_stub through cold-boot recovery, and you get the same `pstub: tickN Z=... newjob=... active=p077777@...` trace as on hardware. Example after ~30s of simulated time:

```
I (3624) st7789: ST7789 ready: 320x240 landscape
I (3644) dsky: display_hal up: 320x240, strip_h=80
I (3644) app: loading ROM Comanche055 (73728 bytes)
I (3983) app: serial_input: ready (UART0). type V/N/+/-/E/C/P/R/K/0-9 to drive DSKY
...
I (36790) chrouter: alarms RuptLock=0 NW=0 ... FAILREG=[01107,00000,77777] RegZ=04706 cyc=6283265
I (36890) pstub: tick770 Z=05233 newjob=077777 active=p077777@002703 wakestal=-1
```

The Espressif qemu-xtensa fork emulates the ESP32 SoC at the register level — same chip the WROOM-32 packages, so the firmware runs unchanged. **What's not emulated:** WiFi radio (web DSKY offline), touch controllers (no XPT2046/CST820 model). **What does run:** `agc_engine`, `peripheral_stub` (rescue chain + force_dispatch_charin), `channel_router`, `display_hal` over virtual ST7789, `agc_task` pinned to APP_CPU, the new `serial_input_task` (see below).

#### Injecting DSKY keypresses in QEMU

Two paths — pick the one that matches your host:

**A. Serial console (Linux/macOS-friendly, flaky on Windows).** `main/app_main.c` starts `serial_input_task` which reads ASCII from UART0 and maps `V N + - E C P R K 0..9` to DSKY keycodes via `channel_router_post_key`. In `idf.py qemu monitor` just type `RV35E` (or any sequence). On real hardware over USB-serial this works perfectly. **On Windows hosts** the `-serial stdio` pipe to QEMU is unreliable when fed by `subprocess.PIPE` (verified — bytes get silently dropped); use `idf.py qemu monitor` with the keyboard, or use GDB injection instead.

**B. GDB injection (rock-solid on any host).** Bypasses the UART chardev entirely.

```powershell
# Terminal A — start QEMU and halt the CPU waiting for GDB:
. C:\esp\v6.0.1\esp-idf\export.ps1
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.qemu" qemu --gdb monitor

# Terminal B — attach gdb with the espAGC helper script loaded:
. C:\esp\v6.0.1\esp-idf\export.ps1
idf.py gdb -x tools\qemu.gdbinit
(gdb) continue       # let the firmware boot
(gdb) <Ctrl-C>       # interrupt after a few seconds
(gdb) call_key 18    # R (RSET)
(gdb) call_key 17    # V
(gdb) call_key 3     # 3
(gdb) call_key 5     # 5
(gdb) call_key 28    # E (ENTR)
(gdb) continue       # watch the lamp test run
(gdb) <Ctrl-C>
(gdb) agc_state      # dump A/L/Q/Z/FB/slot/cycle
(gdb) watch_slot     # break on next write to slot-0 PRIORITY
```

See `tools/qemu.gdbinit` for the full helper definitions including `run_v35e_demo` (scripted RV35E run) and notes per keycode. The slot-corruption watchpoint (`watch_slot`) is the recommended way to chase the V37E00E×2 bug captured in `test_slot_writes`.

## Host tests

Three layers of host tests run in ~2 seconds total. No hardware required.

```powershell
cd tests\host
mingw32-make run    # gcc must be on PATH
```

### Layer 1 — pure logic (`-D__embedded__` against yaAGC + agc_init.c + a tiny IO stub)

| Test | What it asserts |
|---|---|
| `test_rom_load` | ROM bank-reorder loader produces the right Fixed[bank][word] layout |
| `test_engine_boot` | 50 000 AGC cycles run cleanly; PC moves off boot vector |
| `test_channel10_emit` | Engine emits writes to ch010 (DSKY display) + ch011 (status) within 200 000 cycles |
| `test_keypad_hit` | 320×240 keypad cell centers map to the right `dsky_key_t` codes; out-of-bounds returns -1 |

### Layer 2a — engine wired to the *real* channel_router

These compile `components/channel_router/channel_router.c` against host shims (`tests/host/include/freertos/*.h`, `esp_log.h`) so we exercise the actual code path the firmware uses.

| Test | What it asserts |
|---|---|
| `test_alarm_at_boot` | Engine survives 5 M cycles after fresh-start; prints the agc_t alarm flags + resolved dsky_state for diagnosis |
| `test_p00_select` | Boot, RSET, V37E00E — engine reaches a state where COMP ACTY blinks |
| `test_lamp_test` | Boot, RSET, V35E — flash V/N + indicator lamps light within a window |
| `test_rset_clears_alarms` | After RSET, `dsky_state.restart` clears (regression guard for the WriteIO routing fix) |
| `test_replay_apollo11_launch` | Replays `third_party/virtualagc/yaDSKY2/Apollo11-launch.canned` (a yaDSKY2 recording of real Luminary output) directly through `channel_router_on_output`, asserts decoded PROG/VERB/NOUN/COMP ACTY match the launch transcript. Validates the entire decode pipeline against ground truth. |

### Layer 2b — renderer pixel tests

Compile `dsky_render_320x240.c` + `font5x7.c` + `dsky_layout.c` + `dsky_keypad_320x240.c` against host gcc (zero ESP-IDF dependencies past `<string.h>`). Render into an in-memory RGB565 framebuffer and assert.

| Test | What it asserts |
|---|---|
| `test_render_blank` | Blank dsky_state renders to a non-empty buffer; FNV-1a hash is stable across builds (drop a font or layout tweak — fail loudly, update the hash deliberately) |
| `test_render_prog_lit` | After setting PROG=12, VERB=16, NOUN=65, region assertions confirm at least N amber pixels land inside the labelled cell rectangles. Robust to font/spacing tweaks; fails only when a digit literally isn't painted |

### Reference harness — comparing against vanilla yaAGC

`yaagc_ref.exe` is a minimal reference binary that links the **upstream** yaAGC engine + `agc_engine_init.c` + `NullAPI.c` directly. It's the cleanest way to see what cold-boot Luminary does without any of our integration layered on top — useful for distinguishing "our bug" from "upstream behavior."

```powershell
cd tests\host
mingw32-make yaagc_ref.exe
./yaagc_ref.exe                          # vanilla yaAGC + Luminary099
./yaagc_ref.exe --sim                    # + peripheral_stub_step every 1k cycles
```

Outputs `Z`, `PRIORITY`, `POLISH`, `LOC`, `REDOCTR` at cycle 1k / 5k / 10k / 30k / 100k / 500k / 1M / 2M, plus a Z-histogram of the last 10k cycles. The histogram makes deadlocks visible (one or two Z values dominating = stuck loop) versus healthy execution (broad distribution).

### Why this layering

Three-layer pattern adopted from `dosNew/esp-dos/docs/testing.md`:
- **Layer 1** runs in milliseconds, gates *everything*. Logic correctness only.
- **Layer 2a** catches integration regressions (the kind of thing where ch010 row-mapping was wrong but every Layer 1 test passed). Replay against the canned Apollo 11 launch transcript is the strongest assertion in the repo — if the decoder is wrong, the replay diverges from ground truth.
- **Layer 2b** catches "the digit suddenly isn't on the screen anymore" regressions without needing the LCD wired up.
- **Layer 3 hardware** is reserved for things only hardware can show: backlight wiring, panel orientation, touch calibration, ROM-vs-peripheral interactions Luminary cares about.

## Dependencies

- ESP-IDF v6.0+
- MinGW-w64 / gcc / clang on the host (for yaYUL)
- All third-party code is in `third_party/` as submodules — `git submodule update --init --recursive` if you didn't `--recurse-submodules` at clone time.

## License

**GPL v2.** yaAGC's license carries through. See `LICENSE` and the
`COPYING` file inside `third_party/virtualagc/`.
