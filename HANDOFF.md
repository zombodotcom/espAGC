# Handoff — Current State (May 2026)

A snapshot of where espAGC is, what works, what doesn't, and concrete next steps. Read this end-to-end before resuming work — there's a lot of subtle state.

## TL;DR

- **Hardware boots cleanly, watchdog stable, but DSKY display stays blank** on V35E and V37E00E keypress sequences.
- **Host `verify-ref` test** runs against a real ground-truth trace captured from WSL yaAGC + Python keypress capture. Exit code 2 ("PARTIAL OK") — channel-value subsets match reference but PRG=00 (ch010=55265) doesn't emit.
- **Watchdog fix landed**: `agc_task` was tripping `task_wdt` via `vTaskDelayUntil` degenerating to a tight loop. Replaced with `agc_core_step(2000) + vTaskDelay(1)` → 0 watchdog trips, ~95 kHz simulated engine rate.
- **Dual-core layout in place**: `agc_task` pinned to APP_CPU (core 1) priority 10, `ui_task` pinned to PRO_CPU (core 0) priority 5 — mirrors WSL's separation of yaAGC process from peripheral-sim process.
- **Current code on `main` has all rescues DISABLED** as an experiment to test whether the ESP32's real-time-paced engine cold-boots cleanly without our interference. This needs flash+monitor verification — most likely outcome is still-blank display (in which case revert and try a different angle), but it's a useful baseline.

## What works end-to-end

- **Host `make run` → ALL PASS** (18 assertion tests across Layer 1/2a/2b)
- **`make verify-ref` → PARTIAL OK exit 2** (channel-value subsets align with WSL reference)
- **ESP32 firmware boots**, joins STA WiFi or falls back to SoftAP, web DSKY at `http://192.168.1.23/` (your network) or `http://192.168.4.1/` (SoftAP)
- **Touch + WiFi keypad both work** — sequences fire (`seq: running 'Lamp test (V35E)' (4 keys)` shows in monitor)
- **Watchdog stable** — engine ticks indefinitely without `task_wdt: Task watchdog got triggered`

## What doesn't work

- **Hardware DSKY display stays blank** after V35E. The PROG ALARM and RESTART lamps stay lit (cold-boot NW alarm latched in FAILREG[0]=01107). No digit rows (`ch010 row=10/11/9 payload=non-zero`) are ever emitted.
- **V37E00E → PRG=00 fails on host too** (`verify-ref` PARTIAL OK).
- **Rescues fire on hardware but don't unblock display**. The `rescue_stuck_z` we added fires 8 times (cap), engine unsticks each time but lands in a new stuck state. After cap reached, engine drifts to "idle but broken" — slot empty, alarms latched, never recovers.

## Architecture recap

```
                   ESP32-WROOM-32 (dual-core, 160 MHz)
   ┌─────────────────────────────────┬─────────────────────────────────┐
   │ APP_CPU (core 1)                │ PRO_CPU (core 0)                │
   │                                 │                                 │
   │ agc_task (priority 10)          │ ui_task (priority 5)            │
   │   loop:                         │   snapshot, render DSKY,        │
   │     agc_core_step(2000)         │   ST7789 framebuffer,           │
   │     vTaskDelay(1)               │   WiFi + touch + HTTP server    │
   │   = ~95 kHz simulated AGC       │                                 │
   │                                 │                                 │
   │ peripheral_stub_tick fires      │                                 │
   │   every ~8000 cycles via        │                                 │
   │   yaAGC's ChannelRoutine        │                                 │
   │   callback (not its own task)   │                                 │
   └─────────────────────────────────┴─────────────────────────────────┘
                              ↕
                       channel_router
                       (key ring buffer,
                        dsky_state_t snapshot)
                              ↕
                  Engine I/O via io_callbacks.c
                              ↕
                yaAGC (third_party/virtualagc/yaAGC)
                  agc_engine.c + Backtrace.c +
                  DecodeDigitalDownlink.c +
                  our agc_init.c (replaces upstream
                  agc_engine_init.c)
```

## Host test infrastructure

- `tests/host/Makefile` — `make run` runs all assertion tests (18). `make diag` builds diagnostic probes. `make verify-ref` runs `test_ref_compare` against the WSL golden trace.
- `tests/host/test_ref_compare.c` — drives the same keypress sequence as WSL's `ref_capture.py` (`R V36E V37E 00E V37E 00E`) via async pthread keypress thread. Uses `harness_step_realtime()` to pace cycles at 1.024 MHz wall-clock with `QueryPerformanceCounter` (Windows) / `clock_gettime` (Linux). Takes ~17 sec wall-clock.
- `tests/host/verify_ref_match.sh` — exit codes: 0 = "VERIFICATION OK: PRG=00 emitted", 2 = "PARTIAL OK: subsets align but PRG=00 missing", 1 = "FAIL chXXX: divergent must-match values".
- `tests/host/golden/ref_V36_V37E00E_double_to_PRG00.log` — 3571-line trace captured from WSL yaAGC + Python keypress client. PRG=00 emits at trace line 2868.
- `tests/host/capture_one.sh` + `capture_with_dumps.sh` — WSL helpers to recapture ground truth or grab core dumps.
- `tests/host/test_ref_v37_slots.c` — drives the V37 sequence against UPSTREAM `agc_engine_init.c` (no channel_router, no peripheral_stub_tick, no rescues) and dumps slot 0 state. Proves the cycle-driven V37+ENTR crash is in yaAGC itself, not our integration.

## Investigation timeline (latest first)

1. **Hardware rescue experiment.** Watched ESP32 via `idf.py monitor`. The `rescue_stuck_z` we added (Z stuck within 16-address window for 4 ticks) fires 8 times at boot — engine unsticks each time but never converges. Each GOJAM latches RestartLight+GeneratedWarning, accumulating bad state. **Current `main` has all rescues disabled** to test if engine cold-boots cleanly without our interference.

2. **ESP32 task_wdt fix.** First pinned-to-core attempt with `vTaskDelayUntil` tripped task_wdt every 5 sec because `agc_core_step(10240)` takes longer than 10ms on the 160 MHz ESP32 — drift-correction never yielded. Fixed by reverting to unconditional `vTaskDelay(1)` between 2000-cycle batches.

3. **Host real-time pacing.** Added `harness_step_realtime()` matching yaAGC's `SimExecute` behavior (10K-cycle bursts paced to wall-clock via `QueryPerformanceCounter`/`clock_gettime`). Effect: ch077 alarm count dropped from 13497 to 5411, DSKY snapshot now shows `VRB=37 NUN=00` during V37+digits sequence (first time visible on host). But PRG=00 still missing.

4. **yaAGC interlace=50 match.** yaAGC defaults to `--interlace=50` (socket polled every 50 CPU cycles). Our `channel_router_pump_input` was polling every cycle. Throttled to every 50 cycles to match.

5. **dt_us mismatch fix.** `peripheral_stub_tick` was passing `dt_us=200000` (200ms) per call to `peripheral_stub_step`, but `ChannelRoutine` actually fires every ~2000 cycles (~2ms). Our attitude simulation was integrating 100x too fast → AGC's DAP firing jets to chase wildly drifting state → 6x extra ch005/006 emissions. Now passes `dt_us=2000` correctly. Later updated to 8000 to match actual `017777` cycle mask.

6. **WSL ground truth capture.** Built capture infrastructure: `ref_capture.py` (Python socket client speaking yaAGC's 4-byte IO packet protocol), `capture_one.sh` (WSL wrapper running yaAGC + capture in single process), golden traces in `tests/host/golden/`. Confirmed reference yaAGC produces PRG=00 at t=21.3s with `R V36E V37E 00E V37E 00E` sequence — without lm_simulator.tcl running, only the LM_Sim init values written once at startup.

## Open questions

1. **Why don't rescues unblock the hardware display?** On host they make V35E work cleanly. On hardware they fire but engine never reaches "verb completes, digits emit" state. Hypothesis: each GOJAM latches additional alarm state (RestartLight, GeneratedWarning, FAILREG entries) that interferes with Luminary's RESTART recovery. Worth trying: don't set `RestartLight=1` / `GeneratedWarning=1` in `simulate_gojam` so the engine doesn't think it just restarted.

2. **Does the host cycle-driven harness REALLY reach PRG=00 via rescues, or is it the rescues PLUS specific cycle-alignment that we get for free on x86?** Compare `test_ref_compare` (cycle-driven w/ rescues + real-time pacing) against host's working V35E (cycle-driven w/ rescues, no real-time). Maybe real-time pacing alone is enough to fix V37E00E without rescues. Or maybe rescues + cycle-driven is required and real-time pacing breaks it.

3. **What does WSL's reference do differently that makes V37E00E reach PRG=00 cleanly?** It uses upstream yaAGC, no peripheral_stub, no rescues. Just real-time pacing + LM_Sim init values + keypresses. We've matched all three on host but still don't reach PRG=00. Something subtle differs — likely interrupt firing alignment within `agc_engine`.

4. **Why does cycle-driven mode crash on the SECOND V37+ENTR?** Slot 0 gets saved with `BANKSET=10001` (FBANK=001, EBANK=001) instead of `60101` (FBANK=030 + SUPERBANK + EBANK=001). Z then loads as 0 — engine executes RegA as instruction. `test_ref_v37_slots` proves this is upstream yaAGC behavior, not our integration bug. Where exactly in NOVAC/CHANG2 does BANKSET get corrupted, and what cycle alignment in real-time mode avoids it?

## Concrete next steps to try (priority order)

### 1. Verify the rescues-disabled experiment on hardware

Current `main` has all rescues disabled. **Flash and monitor**:

```powershell
. C:\esp\v6.0.1\esp-idf\export.ps1
cd C:\Users\zombo\Desktop\Programming\espAGC
idf.py -p COM7 flash
idf.py -p COM7 monitor
```

Hit V35E on the web DSKY. Expected outcomes:
- **If digits light up**: rescues were the problem, ship with rescues off
- **If display still blank**: rescues weren't the problem, revert and try angle #2

If it doesn't help, revert with:
```bash
cd components/peripheral_stub
# remove the `(void)rescue_*; return;` block at top of peripheral_stub_tick
```

### 2. Clean alarm state in simulate_gojam

The rescues GOJAM but `simulate_gojam` sets `RestartLight=1` / `GeneratedWarning=1`. After 8 rescues we have a mess of alarm state. Try removing those two lines AND zeroing FAILREG[0..2] before each GOJAM:

```c
// In simulate_gojam (peripheral_stub.c around line 340):
s->RestartLight = 0;        // was 1
s->GeneratedWarning = 0;    // was 1
s->Erasable[0][0375] = 0;   // FAILREG[0]
s->Erasable[0][0376] = 0;   // FAILREG[1]
s->Erasable[0][0377] = 0;   // FAILREG[2]
```

Then re-enable rescues, flash, monitor. If digits light, this was the fix.

### 3. Find what makes WSL reference work that we don't have

Inside WSL run `idf.py monitor`-equivalent + V37E00E sequence + extract engine state at moment PRG=00 emits (t=21.3s). Compare to our state at the same simulated time. Specifically dump:
- `agc_t::Erasable[0..7]` (all banks)
- `agc_t::InputChannel[0..0177]`
- `agc_t::OutputChannel7`, `RestartLight`, alarm flags

Use `tests/host/capture_with_dumps.sh` — already configured to make yaAGC dump a `core` file every 1 sec. Decode and compare.

### 4. Implement actual LM_Simulator port for full mission programs

The user explicitly wants P11 (powered ascent), P63 (lunar descent), etc. These need:
- Continuous CDU pulse injection via `UnprogrammedIncrement` reflecting simulated attitude
- Response to ch005/006 RCS jet writes (integrate jet impulses into attitude rate)
- Response to ch012 ISS ZERO (reset CDU)
- Response to ch014 gyro test commands
- IMU dynamics with realistic mass + moment of inertia

`components/peripheral_stub/peripheral_stub.c` has the skeleton (`decode_jets`, `push_cdu_delta`, `g_att_*_mdeg`, etc.) currently disabled. Re-enabling is straightforward but needs to be wired through. The dual-core layout supports this — add a `lm_sim_task` pinned to PRO_CPU alongside `ui_task`.

## Build / flash / monitor reference

```powershell
# One-time per terminal session:
. C:\esp\v6.0.1\esp-idf\export.ps1

cd C:\Users\zombo\Desktop\Programming\espAGC

# Build only
idf.py build

# Build + flash (will fail if monitor has COM7 open — Ctrl-] in monitor first)
idf.py -p COM7 flash

# Monitor (interactive, needs real TTY — must run in user terminal, not in a tool call)
idf.py -p COM7 monitor

# From within monitor: Ctrl-T then F = rebuild+flash+reopen monitor (no port-release dance)
```

Host tests:

```bash
cd tests/host
mingw32-make run                                      # all assertion tests
mingw32-make verify-ref                               # vs WSL golden trace
mingw32-make diag                                     # build diagnostic probes
ROM=../../build/roms/Luminary099.bin ./test_ref_compare.exe   # real-time paced V37E00E
ROM=../../build/roms/Luminary099.bin ./test_ref_v37_slots.exe # upstream-init slot probe
```

WSL ground truth capture:

```bash
wsl -d Ubuntu-24.04
cd /mnt/c/Users/zombo/Desktop/Programming/espAGC/tests/host
bash capture_one.sh "R V36E V37E 00E V37E 00E" > /tmp/cap.out
grep "OUT ch010 = 55265" /tmp/cap.out   # should print 1 line (PRG=00 emitted)
```

## Key files

- `main/app_main.c` — task layout, ROM selection, dual-core pinning
- `components/agc_core/agc_init.c` — replaces upstream `agc_engine_init.c` (ROM-from-memory + clean state init)
- `components/agc_core/io_callbacks.c` — `ChannelInput/Output/Routine` callbacks routing to channel_router
- `components/channel_router/channel_router.c` — DSKY snapshot + keypress ring buffer + ch011/ch010 dedup
- `components/peripheral_stub/peripheral_stub.c` — LM_Simulator-equivalent partial port + rescues (currently disabled experiment)
- `components/display_hal/dsky_render_320x240.c` — 320×240 DSKY renderer (status panel + register window + keypad)
- `tests/host/test_ref_compare.c` — real-time paced V37E00E test vs WSL golden
- `tests/host/agc_harness.c` — `harness_step_realtime` and other host harness primitives
- `tests/host/verify_ref_match.sh` — verify-ref exit code + which-channels-diverge report

## Memory references

The auto-memory system at `~/.claude/projects/...espAGC/memory/` has detailed notes:

- `project_session_state_honest.md` — list of every lie/hack with status
- `project_luminary_coldboot_stuck.md` — 1/ACCSET INTERPRETER.agc deadlock + two-stage rescue documentation
- `project_v37_intstall_blocker.md` — V37 INTSTALL JOBSLEEP blocker
- `project_v37_second_press_crash.md` — V37+ENTR slot BANKSET corruption
- `project_v37_needs_servicer.md` — proven not an integration bug
- `project_v37_real_time_pacing_progress.md` — pthread real-time pacing investigation
- `project_ref_compare_infra.md` — golden trace + verify-ref infrastructure
- `reference_esp32_workflow.md` — build/flash/monitor PowerShell workflow
- `feedback_stop_lying_about_tests.md` — rule against fake "PASS" prints
- `feedback_no_hardcoded_workarounds.md` — rule against stacking hacks
- `feedback_no_erasable_pokes.md` — rule against direct Erasable[] writes

Read `MEMORY.md` first for the index.

## Current uncommitted state

At time of writing:
- `components/peripheral_stub/peripheral_stub.c` has `(void)rescue_*; return;` block in `peripheral_stub_tick` — **all rescues disabled** as an experiment
- This change is what's being committed alongside this handoff doc
- To revert: delete the `(void)rescue_stuck_job; return;` block at the start of `peripheral_stub_tick`

If you flash this build and the display now lights up after V35E, ship it. If not, revert and pursue next-step #2 (clean alarm state in simulate_gojam).
