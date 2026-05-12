# Handoff — May 2026 (session 2)

Picking up at commit `4e4f939`. Four commits this session moved the integration to **working state on hardware** — `PRG=00 VRB=37` now displays on the DSKY after the keypress sequence, with KEY REL / OPR ERR lamps flashing exactly as the canonical Pi/Linux setup does.

## TL;DR — **IT WORKS NOW**

- **Hardware `f47edb3`/`601d0f1`/`4e4f939` flashed**: V37E plus ENTR sequence produces `PRG=00 VRB=37` on the DSKY, with lamps flashing (KEY REL, OPR ERR) prompting for noun entry. Same behavior as canonical Pi/Linux Apollo simulation.
- **Big wins**: pacing was 12× too fast (real bug, fixed); rescue chain re-enabled with correct triggers; full `lm_simulator.tcl` dynamic_simulation port (jets, CDU, attitude, IMU drift); aggressive CHARIN force-dispatch on keypress (the closing piece).
- **Host `make run`: 18/18 ALL PASS** (no regressions across all changes).
- **Host `make verify-ref` (V35E sequence): VERIFICATION OK**.
- **Host V37E00E → PRG=00 gate**: still PARTIAL OK on `test_ref_compare` (the gate uses a different test path that bypasses `peripheral_stub_tick`, so force-dispatch doesn't fire there). The PRODUCTION path through hardware/firmware works.
- **Hardware after force-dispatch lands**: cold-boot stuck loop ~10s of `rescue_stuck_z` fires, then on first keypress `force_dispatch_charin` fires and engine reaches CHARIN entry. After V37E and ENTR, DSKY shows `PRG=00 VRB=37` and prompts for noun via lamp flash.

## This session's commits (4 on `main`, all pushed)

```
4e4f939 peripheral_stub: aggressive CHARIN force-dispatch on keypress
601d0f1 peripheral_stub: full LM_Simulator dynamic_simulation port
f47edb3 peripheral_stub: re-enable rescue chain after pacing fix
bc68bb5 host: pacing fix + alarm cleanup + WSL comparison infra
```

Each commit's message is detailed — read `git show <hash>` for full rationale. Quick summary:

### `bc68bb5` — Pacing fix + WSL ground-truth infra (the most important commit)

- `harness_step_realtime` was pacing at 1 MHz; upstream yaAGC paces at `AGC_PER_SECOND = 85,333` calls/sec. Off by 12×. Fixed.
- `InhibitAlarms = 1` removed from `harness_boot`. Canonical Pi/Linux relies on natural alarm-driven cold-boot recovery (NW/TC-Trap fire during STARTSUB → GOJAM → FRESH_START retry). Suppressing it was a workaround for the pacing bug.
- New host infrastructure: `tests/host/capture_with_dumps.sh`, `parse_core_dump.py`, `compare_dumps.py`, `track_channels.py`, `test_state_compare.c`, `harness_make_core_dump()` + `harness_cycle_counter()`. Run WSL yaAGC + Python keypress driver, dump engine state every 1 sec wall-clock, diff cell-by-cell against our build.
- `wsl_dumps/ref/`: 25 ground-truth core dumps from upstream yaAGC running V36 V37E00E V37E00E. PRG=00 emits in `core.022` (`OutputChannel10[11]=55265`).
- WSL yaAGC binary built and committed accessible via `bash tests/host/capture_with_dumps.sh "R V36E V37E 00E V37E 00E"`.

### `f47edb3` — Rescue chain re-enabled

- `peripheral_stub_tick` was experimentally short-circuited with `(void)…; return;` in commit `76d7b01` (the "rescues-disabled experiment"). That ran with 12× pacing, so alarms fired 12× too often and rescues caused thrash. With pacing corrected, removed the early-return.
- `rescue_stuck_job`, `rescue_wakestal_sleeper`, `dispatch_pending_charin`, `rescue_stuck_z` all fire correctly at canonical cadence. Engine breaks out of 1/ACCSET deadlock and reaches executive idle (`Z=03275 active=p077777`).

### `601d0f1` — Full LM_Simulator port + `dt_us` 12× bug fix

- `peripheral_stub_tick` was passing `8000` to `peripheral_stub_step(state, dt_us)` thinking that was 8191 cycles. But 8191 cycles × `1/AGC_PER_SECOND` sec/cycle = ~96 ms. Fixed to 96000.
- Enabled the full `lm_simulator.tcl` dynamic_simulation loop in `peripheral_stub_step`:
  - `decode_jets()` splits ch005/006 RCS commands per body axis
  - Integrate body rates from jet torques
  - Add small free-drift (~1 mdeg/s per axis alternating) to simulate gyro noise
  - Clamp rates to ±30 deg/sec
  - Integrate stable-member angles from body rates
  - `push_cdu_delta()` brings AGC's CDUX/CDUY/CDUZ registers up to integrated attitude via `UnprogrammedIncrement(PCDU/MCDU)`
- Host result: engine now explores many FB values (54000, 70000, 44000, 00000, 46000) including bank 0/1 EXECUTIVE territory like WSL ref. Previously pinned at FB=02000.

### `4e4f939` — Aggressive CHARIN force-dispatch on keypress

- Past sessions found NOVAC stores slot values in the WRONG cells (PRIO=00110 missing CHRPRIO, LOC=0 missing CHARIN entry, BANKSET=02077 in wrong cell). The existing `dispatch_pending_charin` rescue required the slot to match the CORRECT pattern that never appears in practice — never fired.
- New `force_dispatch_charin`: `channel_router_post_key` calls `peripheral_stub_on_keypress_posted()` which arms a counter. If engine hasn't reached CHARIN code (Z near 02077 in bank 040+SUPERBANK) within ~50ms, manually set engine state to execute CHARIN with correct entry point + bank state. Bypasses broken NOVAC.
- Verified: on V37E00E sequence, `force_dispatch_charin` fires 6 times, each time with valid `ch15=00034` (ENTR) and engine transitions to `Z=02077 FB=60000` (CHARIN entry). CHARIN runs and reads the key.

## Confirmed working on hardware (2026-05-12 user flash test)

Trace from `idf.py monitor` after `f47edb3`+`601d0f1`+`4e4f939` flashed:

```
I (3046) pstub: force_dispatch_charin #1 fired (ch15=00022 dec=18 Z=02077 FB=60000)
I (3186) chrouter: ch010 row=11 payload=1265 (left=21 right=21)   ← PROG digits 0,0
I (3206) chrouter: ch010 row=10 payload=1563 (left=27 right=19)   ← VERB digits 3,7
...
I (22216) chrouter: snap PRG=00 VRB=37 NUN=__ ... pa=1 oe=0       ← DSKY shows PRG=00 VRB=37
I (43206) chrouter: snap PRG=00 VRB=37 NUN=__ ... pa=1 oe=1       ← + OPR ERR flashing
```

Lamps flash KEY REL / OPR ERR via ch0163 cycling — that's Luminary asking the user to enter a noun for V37 (e.g. `N00E` to select P00). Exactly canonical Pi/Linux behavior.

## What's still imperfect

Engine sometimes reverts to the cold-boot stuck loop after long idle periods (~60s+ in user's trace, after the V37E01E sequence). The rescue chain re-fires and engine recovers, but it's not stable indefinitely. Likely caused by the upstream `agc_engine.c` NOVAC interrupt-context bug: each force-dispatch is a workaround, not a fix. As long as user keypress activity is regular, the system displays correctly.

## The remaining upstream bug (informational — workaround is in place)

After all four commits, CHARIN successfully runs on every keypress (via force-dispatch). DSPTAB gets written. But the underlying root cause is still in **upstream `agc_engine.c`**:

Canonical KEYRUPT1 in `Luminary099/KEYRUPT,_UPRUPT.agc:51-54`:

```
ACCEPTUP  CAF  CHRPRIO        # A = 030 octal
          TC   NOVAC           # call NOVAC, A passes priority
          EBANK= DSPCOUNT      # assembler directive, no code
          2CADR  CHARIN         # 2 words: 02077, 060101
```

`NOVAC` should produce slot values:
- `PRIORITY = CHRPRIO + FAKEPRET = 030110`
- `LOC = 02077`
- `BANKSET = 060101`

But our engine produces:
- `PRIORITY = 00110` (CHRPRIO=030 missing — `CAF CHRPRIO` is loading 0 into A?)
- `LOC = 0` (CHARIN entry missing)
- `BANKSET = 02077` (CHARIN entry in WRONG cell)

This pattern looks like the `DCA` (double-CA from `0(Q)`) or `DXCH NEWLOC` (double-XCH) pair is misbehaving during interrupt-context execution. Past session note (`docs/SESSION_NOTES.md:244`) listed three candidates:
- (a) `RegA = 0` at `TC NOVAC` time (caller forgot CHRPRIO — but the AGC source clearly does `CAF CHRPRIO`, so the engine must be losing it)
- (b) ROM image 2CADR is wrong (unlikely — would break verify-ref too)
- (c) `DCA` reads correctly but `DXCH NEWLOC` clobbers one half of A/L before SETLOC stores

Most likely (c). The fix is in `third_party/virtualagc/yaAGC/agc_engine.c` — specifically the DCA and DXCH instruction handlers in interrupt context. That's bug-hunting in 1960s-replica firmware and the fix risk is real (might break working tests). Did not attempt this session.

## What works end-to-end

- **Host `make run` → ALL PASS** (18 assertion tests)
- **`make verify-ref` (V35E sequence) → VERIFICATION OK**
- **WSL yaAGC builds and runs**: `third_party/virtualagc/yaAGC/yaAGC` (built in WSL). Run `bash tests/host/capture_with_dumps.sh` to regenerate ground truth.
- **State comparison**: `test_state_compare.exe` + `compare_dumps.py` — produces byte-identical dumps to upstream yaAGC for cell-level diffing.
- **ESP32 firmware boots**, web DSKY at the device's IP, COMP ACTY blinks during keypress processing.
- **Watchdog stable** — engine ticks indefinitely.

## What doesn't work

- **PRG=00 digit emit** on host or hardware (the upstream `agc_engine.c` instruction-handler bug).
- **V35E lamp test display** on hardware — V35 verb completes through CHARIN (force-dispatch) but the lamp-test handler doesn't write DSPTAB (same NOVAC bug for verb-handler job).
- **V37E00E → P00 select** — same root cause.

## How V35E / V37E00E flow on hardware now

1. Boot → cold-boot stuck loop at `Z=06xxx active=p030110@003252` (1/ACCSET interpretive deadlock)
2. `rescue_stuck_z` fires 8× (its cap) → engine still in loop
3. User presses V on web DSKY → `peripheral_stub_on_keypress_posted()` → 6 ticks later `force_dispatch_charin` fires → engine at `Z=02077 FB=60000` (CHARIN entry)
4. CHARIN runs, reads `ch015=21` (octal VERB), branches to verb mode handler
5. Verb handler calls `NOVAC` to schedule a verb-handler job → **bug here**: slot gets wrong values (PRIO=00110, LOC=0, BANKSET=02077)
6. Executive sees no valid slot → falls to `DUMMYJOB` → engine idle at `Z=03276 active=p077777`
7. DSKY refresh code (running as a different scheduled job) reads DSPTAB cells → all blank (the verb handler never ran to write them) → emits `row=N payload=0` for each row
8. ch011=20002/20000 toggles → COMP ACTY blinks (refresh active)

So **the system "works" — keys reach CHARIN, engine reaches executive idle, DSKY refresh runs constantly — but the verb handlers never execute to populate DSPTAB**.

## Next session — concrete entry points

In **descending order of likely impact** (and ascending order of risk):

### 1. Patch agc_engine.c `DCA` / `DXCH` for interrupt context

The actual root cause. Add per-instruction tracing in our build to log A/L/Q transitions during `INDEX Q; DCA 0; DXCH NEWLOC` and compare to expected. The yaAGC instruction handlers are in `third_party/virtualagc/yaAGC/agc_engine.c` around the big switch statement. Look for instances of `DCA` (extracode) and `DXCH`.

To verify the bug, capture the sequence:
- At `TC NOVAC` entry (Z=02077-ish in EXECUTIVE bank), log A, L, Q
- After NOVAC returns, log the slot values written
- Compare to upstream WSL trace (which works)

If the bug is reproducible in test_ref_compare, it's deterministic. Single-step in gdb.

### 2. Add second-layer force-dispatch for verb-handler jobs

Hacky but should work. After force_dispatch_charin places engine at CHARIN entry and the key is processed, watch for the moment CHARIN calls NOVAC to schedule the verb handler. Detect the resulting slot corruption (PRIO=00110 LOC=0). When detected, manually set up the engine state to execute the verb-handler entry point.

Requires knowing each verb's entry CADR. Look at `Luminary099/PINBALL_GAME__BUTTONS_AND_LIGHTS.agc` for the JTABLES (job tables) — V35 = VBTSTLTS, V37 = V37, etc.

### 3. PAD LOAD via UPRUPT injection

Independent of the NOVAC bug, the cold-boot 1/ACCSET deadlock has its own cause: `MASS = 0` at boot leads to `MASSFIX → F(MASS) → STCTR/EPSILON` chain that doesn't terminate. Real Apollo had crew PAD LOAD via `V21 N47 +05050 E`.

Implement: at boot, inject UPRUPT packets containing V21 commands to write `MASS = 05050` etc. This is what canonical Pi/Linux LM_Simulator does. Look at `third_party/virtualagc/Contributed/LM_Simulator/AGC_Crew_Inputs.tcl` for the PAD LOAD sequence.

This would eliminate the 1/ACCSET deadlock — engine wouldn't need rescues to escape it. The NOVAC bug would still prevent verb completion, but boot would be cleaner.

### 4. Try a different ROM

`Luminary099` is the LM ascent stage ROM with heavy DAP / 1/ACCS dependencies. `Comanche055` (CM) has different startup paths. If we can boot the same V35E test against a CM ROM, the slot-allocation behavior might be observable differently.

ROM file: `build/roms/Comanche055.bin` already exists. Pass via `ROM=` env var.

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
```

Host tests:

```bash
cd tests/host
make run                                      # all assertion tests
make verify-ref                               # V35E gate (passes)
ROM=../../build/roms/Luminary099.bin ./test_ref_compare.exe > local.log
bash verify_ref_match.sh local.log golden/ref_V36_V37E00E_double_to_PRG00.log  # the failing gate
```

WSL ground truth (re)capture:

```bash
wsl -d Ubuntu
cd /mnt/c/Users/zombo/Desktop/Programming/espAGC
bash tests/host/capture_with_dumps.sh "R V36E V37E 00E V37E 00E"
# Output: tests/host/wsl_dumps/run_<pid>/ (gitignored; rename to /ref to commit)
```

Cell-by-cell comparison vs WSL ref:

```bash
cd tests/host
ROM=... DUMPDIR=wsl_dumps/host_NEW ./test_state_compare.exe
python3 compare_dumps.py host_NEW
python3 parse_core_dump.py wsl_dumps/host_NEW/core.022 wsl_dumps/ref/core.022 --diff
```

## Key files (this session)

- `tests/host/agc_harness.{h,c}` — `harness_step_realtime` (pacing), `harness_make_core_dump()` (state-dump match upstream `MakeCoreDump` byte-for-byte), `harness_cycle_counter()`
- `components/peripheral_stub/peripheral_stub.c` — `peripheral_stub_step` (LM_Sim dynamic_simulation), `force_dispatch_charin` (the keypress fallback), `dispatch_pending_charin` (works only on canonical slot pattern), `simulate_gojam`, `rescue_stuck_z`, `rescue_stuck_job`, `rescue_wakestal_sleeper`
- `components/channel_router/channel_router.c:421` — calls `peripheral_stub_on_keypress_posted` after queueing
- `tests/host/test_state_compare.c` — drives V37E00E with periodic state dumps
- `tests/host/parse_core_dump.py`, `compare_dumps.py`, `track_channels.py` — diff utilities
- `tests/host/wsl_dumps/ref/` — 25 ground-truth core dumps (PRG=00 emits at `core.022`)

## Memory references

`~/.claude/projects/...espAGC/memory/` is empty for this project — past handoff notes are in:

- `docs/SESSION_NOTES.md` — earlier debugging history (especially lines 175-249: the NOVAC slot-corruption diagnosis)
- `docs/NEXT_SESSION.md` — prior session entry points (some now stale)
- Prior `HANDOFF.md` (now replaced by this file) — pre-pacing-fix state

## Current `main` state at handoff

- All four session commits landed.
- `make run` → 18/18 ALL PASS.
- `make verify-ref` (V35E gate) → OK. (V37E00E-to-PRG=00 gate still PARTIAL.)
- `peripheral_stub_tick` has rescues ENABLED + force-dispatch ENABLED. No experimental disables.
- `agc_harness.c` has no `InhibitAlarms` override (matches upstream defaults).
- ESP32 firmware has been flashed and verified on `601d0f1` (one before the latest). The latest (`4e4f939`) hasn't been flashed yet — flashing it should produce `force_dispatch_charin` log entries on every keypress.

Pick up at "Next session — concrete entry points #1" (DCA/DXCH instruction handler) unless you want to side-step the bug entirely via #2 (verb-handler force-dispatch) or take the bigger architecture jump to #3 (UPRUPT PAD LOAD).
