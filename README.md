# espAGC

**An Apollo Guidance Computer running on a $7 dev board.**

A self-contained AGC simulator on the ESP32-WROOM-32 inside the [ESP32-2432S028 "Cheap Yellow Display"](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) — the canonical 2.8″ CYD with 320×240 ST7789 and XPT2046 resistive touch. The board boots straight into a working DSKY: touch-keypad on the screen, web DSKY on its WiFi IP, and a canonical 4-byte protocol TCP listener on port 19850 so it shows up as a drop-in [yaAGC](https://github.com/virtualagc/virtualagc) node to anything that already talks to one.

Both **Luminary 099 (LM)** and **Comanche 055 (CSM)** mission ropes are reassembled at build time from the original AGC source in [virtualagc/virtualagc](https://github.com/virtualagc/virtualagc) via host yaYUL and embedded directly in the firmware. License is **GPL v2** (carries through from yaAGC).

> The engine is the real yaAGC engine. The ropes are the real Luminary 099 / Comanche 055 ropes. The DSKY behaviour is the real DSKY behaviour. The only thing simulated is the spacecraft attached to it — and even that runs through the same channel-I/O protocol the real LM_Simulator on Pi/Linux uses.

---

## Quick start

1. Grab the prebuilt firmware from [the latest release](https://github.com/zombodotcom/espAGC/releases/latest):
   ```
   espAGC-v0.1.0-merged.bin
   ```
2. Flash it (single image at offset 0x0, no IDF setup required):
   ```
   python -m esptool --chip esp32 --port COM<n> -b 460800 write-flash 0x0 espAGC-v0.1.0-merged.bin
   ```
3. Connect a serial monitor at 115200 baud and watch it boot. After ~3 s you'll see something like:
   ```
   I (1330) yaagc_sock: listening on 0.0.0.0:19850
   I (1410) app: espAGC running
   I (2910) wifi_input: got ip: 192.168.1.23 — web DSKY at http://192.168.1.23/
   ```
4. Open the web DSKY at `http://<device-ip>/`, tap **Lamp test (V35E)**, watch the LCD and the browser DSKY light up in lockstep.

If WiFi can't reach your network, the firmware falls back to a SoftAP called `espAGC` at `192.168.4.1`.

---

## What works

| Feature | Status | How to try it |
|---|---|---|
| **V35E lamp test** | ✅ | Tap the canned sequence — all 12 indicators + every 7-seg digit light up |
| **V37E nn E program select** | ✅ | P00 idle, P01 prelaunch, P63 landing braking, P66 manual hover, P68 confirm |
| **V16N36E display GET** | ✅ | R1 ticks up with mission elapsed time |
| **V05N09E display alarm** | ✅ | R1 shows latest program-alarm code |
| **V16N63E P63 monitor** | ✅ | VERB/NOUN render, COMP ACTY flashes during guidance cycles |
| **V16N68E landing analog** | ✅ | Display fields paint; numerics need state-vector init (see below) |
| **Apollo 11 landing transcript** | ✅ | One-click replay of Armstrong/Aldrin's PDI keystroke timeline |
| **PRG=00 (ch010 = 055265)** | ✅ 5/5 | Canonical socket reliability test passes against the device |
| **Comanche 055 (CSM rope)** | ✅ boots | Hold the BOOT button at reset — DSKY comes up in CM mode |
| **Touch / web / serial / TCP keypresses** | ✅ | All four input paths feed the same canonical drain |
| **Descent thrust simulator** | ⚠️ wired, foundation only | Fires PIPA + LR pulses; displays need state-vector init to read them |
| **Realistic ALT / HDOT / TTOGO in P63** | ❌ | Needs initial state vector at PDI — next release |

---

## How to drive it

Four input paths, all converging on the same canonical mask+value drain inside `ChannelInput`:

### 1. Touch screen
Tap the on-screen 19-key DSKY. The keypad is positioned along the right side of the panel; the rest of the screen shows the live DSKY display.

### 2. Web DSKY (`http://<device-ip>/`)
Full 19-button keypad, live mirror of the LCD (PROG / VERB / NOUN / R1 / R2 / R3 + all 12 status lamps polled at 5 Hz), physical-keyboard shortcuts (`V` `N` `+` `-` `E` `C` `P` `R` `K` digits), and a one-click menu of canned sequences plus the descent-thrust toggle.

### 3. Serial console (UART0)
`idf.py monitor` exposes a real TTY. Type letters into the monitor — same map as the web: `V` `N` `+` `-` `E` `C` `P` `R` `K` `0`–`9`.

### 4. Canonical TCP protocol on port 19850
Anything that speaks the [yaAGC 4-byte protocol](https://www.ibiblio.org/apollo/developer.html) can drive the engine remotely. The reference Python driver works without modification:

```bash
py tests/host/hardware_reliability_test.py 192.168.1.23:19850
```

That's the same protocol yaDSKY2, LM_Simulator, and `windows_yaagc_test.py` use. The board appears to those peers as if it were yaAGC.exe running on a workstation.

---

## Tutorial: Apollo 11 landing transcript

Click **Apollo 11 landing transcript** in the web DSKY's canned sequences. The runner replays Armstrong and Aldrin's actual DSKY keystrokes from the [Apollo 11 Lunar Surface Journal](https://www.hq.nasa.gov/alsj/a11/a11.landing.html), paced at 250 ms per key (compressed from the real ~12-minute descent):

| Phase | Keystrokes | What happens |
|---|---|---|
| Reset to clean state | `RSET` | Clears RESTART / OPR ERR / pending verb entry |
| **PDI** (Powered Descent Initiation) | `V 3 7 E 6 3 E` | Selects P63 — Lunar Landing Approach. `active_prio` flips to `p044322@021301` |
| **1201 / 1202 alarms** | `PRO PRO PRO PRO PRO` | The famous five PROCEEDs — Armstrong's "Program alarm!" / Houston's "We're GO on that alarm." Real alarms were rendezvous-radar BBANK leakage; we don't model them, but the historical PROs are part of the sequence |
| **LR monitor** | `V 1 6 N 6 8 E` | V06N68 "LANDING ANALOG DISPLAYS" — LR-alt / forward velocity / altitude rate |
| **Manual ROD** | `V 3 7 E 6 6 E` | P66 manual hover. `+` slows descent 1 ft/s per press, `-` speeds up |
| Armstrong over boulders | `+ + + +` | Armstrong commanded 4 ft/s slower descent over the West Crater boulder field |
| Picking up sink rate | `- -` | After clearing the field |
| **Touchdown** | `V 3 7 E 6 8 E PRO` | P68 Lunar Landing Confirmation. PRO confirms |
| Post-landing | `V 0 6 N 4 3 E` | Surface position display |

Watch the LCD and the web DSKY together — the PROG indicator transitions 00 → 63 → 66 → 68, COMP ACTY blinks during guidance cycles, and the VERB/NOUN flash during data prompts.

---

## Tutorial: Descent thrust simulator

The web DSKY has a **Start descent thrust** button below the canned sequences. It's a foundation, not the full ride — here's what it actually does and what's missing.

### What it does

When you click **Start descent thrust**, `peripheral_stub` flips a flag and the periodic tick starts injecting two pulse streams through the canonical drain (same path TCP peers use, same path the real LM_Simulator on Pi/Linux uses):

- **PIPAZ at ~52 Hz** — Pulsed Integrating Pendulous Accelerometer Z-axis. Each pulse represents 5.85 cm/s of sensed deceleration. 52 pulses/sec ≈ 10 ft/s² thrust, which is the LM Descent Propulsion System's nominal braking acceleration. These go into counter 041 (`PIPAZ`) via `UnprogrammedIncrement`.
- **RNRAD pulses representing landing-radar range** — starts at 50 000 ft (a few seconds before high-gate), ticks down at 300 ft/s, one pulse per 9.38 ft. These go into counter 046 (`RNRAD`).

Toggling the button OFF and ON resets the LR range to 50 000 ft so you can re-run the descent without rebooting.

### How to try it end-to-end

1. Click **P63 landing (V37E63E)** to set MODREG = 63.
2. Type `V 1 6 N 6 3 E` (or click the sequence) so the DSKY is asked to display P63's "altitude / altitude rate / time-to-go" autodisplay.
3. Click **Start descent thrust**.
4. Watch `COMP ACTY` (the green dot on the web mirror, top-left of the lamp panel) start flashing — Luminary's SERVICER cycle is now picking up PIPA pulses every 2 s.

### What you'll see — be set up to be honest with reality

You'll **see** PROG = 63, VERB = 16, NOUN = 63 (or 68), and COMP ACTY blinking — proof the engine is processing the pulse stream. You **won't** see realistic altitude in R1, descent rate in R2, or time-to-go in R3. They'll stay at `+00000`.

P63's autodisplay output isn't a passive readout of the LR range counter — it's the result of a long pipeline:

1. **P63SPOT3 antenna gate** — `THE_LUNAR_LANDING.agc:245` spins on `CA BIT6 / RAND CHAN33 / BZF P63SPOT4`. Until ch033 bit 6 ("LR antenna in position 1") clears, P63 sits in the "PLEASE CRANK THE SILLY THING AROUND" wait loop. **Fixed in this release**: enabling descent thrust now injects ch033 bit 6 = 0 through the canonical drain, so P63 advances past the antenna check.
2. **SETPOS1 → BURNBABY → IGNALG** — Luminary's ignition algorithm. Needs DAP coefficients (LM mass, thrust scaling) from a pre-launch pad load. Without those, IGNALG iterates without converging.
3. **SERVICER's LR cycle** — R12 schedules `LRALT` (`P20-P25.agc:2738`) at the right cadence. `LRALT` reads RNRAD into the AGC's filtered altitude estimate.
4. **State vector integration** — the displayed altitude is `f(R₀ uplinked from MCC, integrated PIPA deltas, LR corrections weighted against estimated altitude variance)`. With `R₀ = 0` everything starts at the lunar surface.

We do (1) now. Steps (2)–(4) need the full Apollo pad-load infrastructure — REFSMMAT initialisation, DAP coefficient load, ch0173 UPRUPT state-vector inject (the protocol MCC used to uplink R₀/V₀ in real time). That's a separate, larger project; see [Roadmap](#roadmap).

What this release does ship is a verified-correct pulse path through the canonical drain: **PIPAZ pulses are reaching the AGC's PIPA counters, RNRAD pulses are reaching the LR range counter, and the antenna-position gate is unlocked.** When the pad-load / uplink work lands, those pulses will start meaning something. Until then, descent thrust is "I/O wiring proven correct" rather than "realistic landing simulator". Honest progress, not theatre.

### What "stop descent thrust" does

Stops the pulse stream. The accumulated PIPA velocity and the RNRAD range counter stay where they were — Luminary will keep computing against the existing state. Click **Start** again and the LR range resets to 50 000 ft for a fresh run.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│ touch_input   web POST /key   serial UART   TCP :19850          │
│      │              │             │             │               │
│      └──────────────┴──────┬──────┴─────────────┘               │
│                            ▼                                    │
│        channel_router_post_key / inject_packet                  │
│                            │                                    │
│                            ▼                                    │
│         ┌────── components/yaagc_socket ──────┐                 │
│         │   synthetic-client byte ring        │                 │
│         │   + canonical mask+value drain      │                 │
│         │   + SocketInterlace=50 throttle     │                 │
│         └─────────────────┬───────────────────┘                 │
│                           ▼                                     │
│         agc_engine (real yaAGC) — ChannelInput per cycle        │
│                           │                                     │
│                           ▼                                     │
│              ChannelOutput                                      │
│              ├─ channel_router_on_output → LCD + web mirror     │
│              ├─ peripheral_stub_on_output → IMU/RCS sim         │
│              └─ yaagc_socket broadcast → connected TCP peers    │
└─────────────────────────────────────────────────────────────────┘
```

The load-bearing realisation: **canonical yaAGC drives WriteIO from *inside* ChannelInput, called once per cycle by agc_engine. The mask-and-value packet flow, the SocketInterlace=50 throttle, and the per-cycle drain order are all load-bearing.** Every in-process port of the main loop we tried before this insight failed V37E00E×2 with the same deterministic state, while yaAGC.exe + Python socket driver passed 5/5. Porting `SocketAPI.c` verbatim — adapted for lwIP / ws2_32 / POSIX — captures all three at once. See `components/yaagc_socket/yaagc_socket.c`.

### Component layout

```
components/
  agc_core/          yaAGC engine wrapper. Cherry-picks engine sources from
                     third_party/virtualagc/yaAGC. agc_init.c loads ROMs
                     from flash instead of stdio.
  apollo_rom/        Runs host yaYUL on virtualagc's Luminary099 / Comanche055
                     mission trees at configure time, embeds via EMBED_FILES.
  channel_router/    Engine channel-I/O ↔ dsky_state_t snapshot. Always
                     drives the LCD/web mirror; routes keys through
                     yaagc_socket_inject_key when the canonical flag is on.
  yaagc_socket/      The canonical SocketAPI port (this release's headline).
                     ChannelInput / ChannelOutput / ChannelRoutine matching
                     yaAGC line-for-line, lwIP BSD sockets, 4-client slot
                     table with slot 0 reserved as the local synthetic client.
  peripheral_stub/   Port of LM_Simulator behaviour: deferred LM_INI through
                     canonical mask+value flow, attitude integration with
                     PCDU/MCDU pulse generation, PIPAZ + RNRAD descent
                     pulse drivers.
  display_hal/       320×240 DSKY renderer. ST7789 panel driver, framebuffer
                     rendered in three 80-row strips, no LVGL.
  touch_input/       XPT2046 resistive driver + 50 Hz poll task.
  dsky_input/        WiFi STA/AP setup + HTTPD serving the web DSKY.
  sequences/         Canned DSKY key sequences (lamp test, P00, P63, Apollo
                     11 landing transcript, etc.) exposed via POST /seq.
  led_status/        3-GPIO RGB status LED driver.
boards/
  board_cyd_2432s028/  Pin map + factory functions returning panel/touch/LED.
main/                  app_main.c — boot sequence + task spawn.
tests/host/            Three-layer host gcc test harness (~ a few seconds).
tools/                 yaYUL host build, yaAGC Windows stubs, QEMU drivers.
third_party/           Submodules: virtualagc, Apollo-11.
```

---

## Building from source

Requires **ESP-IDF v6.0+** and a host C compiler (MinGW-w64 on Windows, gcc/clang on Linux/macOS) for the yaYUL pass that assembles the ropes.

```powershell
git clone --recurse-submodules https://github.com/zombodotcom/espAGC.git
cd espAGC

. C:\esp\v6.0.1\esp-idf\export.ps1            # or whichever path

idf.py set-target esp32
idf.py build                                  # ~2 min cold (host yaYUL + LM/CSM assembly)
idf.py -p COM<n> flash monitor
```

`CONFIG_AGC_YAAGC_SOCKET=y` is the default. The canonical-socket path is the supported path.

### QEMU (no hardware needed)

Espressif's `qemu-xtensa` fork (≥ 9.2) supports `-machine esp32` — the exact SoC in the WROOM-32. Install once:

```powershell
python $env:IDF_PATH\tools\idf_tools.py install qemu-xtensa
. C:\esp\v6.0.1\esp-idf\export.ps1            # re-source so PATH picks up qemu

idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.qemu" build
idf.py qemu monitor                           # boots WROOM image, serial → stdout
```

WiFi PHY isn't emulated (the web DSKY is offline under QEMU). Everything else — engine, peripheral_stub, channel_router, display, touch, the canonical socket layer — runs. Useful for fast iteration without the COM<n> flash cycle.

---

## Tests

```bash
cd tests/host
mingw32-make run                              # full Layer 1 + 2 — passes
mingw32-make test_yaagc_socket_host.exe \
             test_yaagc_socket_local.exe      # canonical-socket reliability harnesses
py yaagc_socket_reliability.py 5              # 5/5 PRG=00 via TCP
```

| Layer | What it tests |
|---|---|
| **Layer 1** — pure logic | ROM loader, engine boot, channel-10 DSKY emit, keypad hit-test |
| **Layer 2a** — engine + real channel_router | Boot alarms, P00 select, lamp test, RSET clears, IMODES, FAILREG, CHARIN dispatch |
| **Layer 2b** — renderer pixel tests | Blank-frame FNV-1a hash, lit-region assertions |
| **Layer 2c** — canonical socket port | `test_yaagc_socket_host` over TCP **5/5 PRG=00**, `test_yaagc_socket_local` via inject path **3/3 PRG=00** |
| `hardware_reliability_test.py` | Drives a live device over TCP — point at `<device-ip>:19850` |

The breakthrough this release ships on was finding `test_yaagc_socket_host` passes 5/5 while every in-process port (`test_canonical_match`, `test_simexecute`, `host_reliability_test`) failed 0/5. The diagnosis is documented across those test files' comments.

---

## Roadmap

For the next release:

- **State-vector init at PDI** — either a ch0173 UPRUPT packet injector (the canonical Apollo digital uplink path) or a typed V21/V22 sequence loading R₀ + V₀ from the Apollo 11 pre-PDI PAD. Once this lands, P63 displays altitude / velocity / time-to-go for real.
- **LR data-good gating** — ch033 bits 4-5 + the LR antenna mode logic so Luminary's `RADARSUP` reads the RNRAD pulses we're already injecting.
- **Full CM peripheral simulation** — Comanche055 boots clean now (LM_INI skipped under CM) but doesn't have CSM-specific channels (ch013 RHC, ch166/167/170 optics) driven yet.
- **Delete the gated-off legacy rescue chain** — currently under `#ifndef CONFIG_AGC_YAAGC_SOCKET` so host Layer-2 tests still link. Migrate those tests to the canonical drain and delete the rescue code outright.

Longer-term:

- 1201/1202 alarm simulation for fidelity.
- More canned sequences: P40 SPS burn maneuver, V41 RCS control, V51 IMU realign.
- AGS (Abort Guidance System) simulation for full mission redundancy.

---

## Credits

- Engine, ropes, protocol — [virtualagc/virtualagc](https://github.com/virtualagc/virtualagc) by Ron Burkey and contributors.
- Original Apollo Guidance Computer software — Margaret Hamilton and the MIT Instrumentation Lab team. The Luminary 099 / Comanche 055 source comments are theirs.
- Apollo 11 keystroke timeline — [NASA Apollo Lunar Surface Journal](https://www.hq.nasa.gov/alsj/a11/).
- Cheap Yellow Display hardware — Brian Lough and the [witnessmenow/ESP32-Cheap-Yellow-Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) community.

License: **GPL v2** (inherited from yaAGC).
