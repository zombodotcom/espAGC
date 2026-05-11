# Next session handoff — real LM_Simulator port, Increment C remaining

Last touched: 2026-05-11. All commits up to `83b9be9` pushed to `origin/main`.

## What works (don't break)

- yaAGC engine and Luminary099 ROM load correctly.
- KEYRUPT1 dispatches on every keypress (`IR5` clears, `ch015` reflects keycode).
- NOVAC allocates slots correctly: `PRIO=30110, LOC=02077, BANKSET=60101, MPAC[0]=keycode`.
- CHARIN runs and `ENDOFJOB`s for fresh keypresses (slots 2-7 free back to `PRIO=77777`).
- **Real periodic peripheral simulator** built today: `peripheral_stub_step(state, dt_us)` writes channels 30-33 with LM_Simulator-equivalent values and pushes CDU PCDU pulses via the engine's `UnprogrammedIncrement` entry. Same `WriteIO()` and `UnprogrammedIncrement()` paths LM_Simulator uses over its socket.
- Host test suite green: `mingw32-make run` → ALL PASS.
- Local yaYUL build at `third_party/virtualagc/yaYUL/yaYUL.exe` for symbol lookups (rebuild listing into `/tmp/luminary.lst`).

## What's blocked

- DSKY digits stay blank. `dsky_state.verb[]` reads `[-1,-1]` even after V35E.
- The specific failure mode (confirmed via `test_ch010_writes.c`): Luminary continuously writes all ch010 rows 1-12, but the digit-row payloads stay at 0 (blank). After V35E only the lamp row (row 12) updates with `payload=050` (NO ATT + GIMBAL LOCK bits set). So Luminary's DSKY-update path runs to *some* depth — it acknowledges keypresses by toggling lamp bits — but the digit-rendering portion never executes.

## Where the engine actually parks

After today's increments (channel feed + CDU pulses), slot 0 transitioned from PRIO=30110 (running 1/ACCS GOODEPS1) to **PRIO=33002**. PRIO33 = `OCT 33000` = `DISPLAY_INTERFACE_ROUTINES.agc:753 — MAKEPLAY raises priority via PRIOCHNG to PRIO33` for fast jobs after wake. The "002" suffix is from `AD` operations inside MAKEPLAY adding the slot offset to the user's priority.

Slot 0 sitting at PRIO=33002 forever means MAKEPLAY isn't reaching its corresponding `PRIOCHNG`-down (or `ENDOFJOB`). Something in the display-interface state machine is waiting on input we still don't provide.

The CHARIN jobs from keypresses (slots 4-7) all schedule correctly at `PRIO=30110` and wait — they lose to slot 0's `PRIO=33002`.

## What still needs to happen — Increment C

Per the approved plan (`C:\Users\zombo\.claude\plans\continue-but-make-a-cozy-lightning.md`), Increment C of the LM_Simulator port:

1. **Trace Luminary's output writes** to channels 5, 6, 11, 13, 14 around the moment slot 0 enters MAKEPLAY. These are the AGC's commands to RCS jets, lamps, alarm-clear, etc. The simulator should *respond* — toggle related input bits, update IMU state, etc. — to convince MAKEPLAY (or whichever caller it's attached to) that its preconditions are satisfied.

2. **Identify what MAKEPLAY is gated on.** Read `DISPLAY_INTERFACE_ROUTINES.agc:743-800` carefully. It branches on `PLAYTEM4`, `FLAGWRD4`, `NBUSMASK`. Those cells need either a sensible boot value or to be set by the simulator in response to specific AGC actions. The cells' meanings are documented in `FLAGWORD_ASSIGNMENTS.agc` — start there.

3. **Implement a minimal physical model**: attitude state (3 floats), respond to channel 5/6 writes by adjusting attitude rate, drive CDU at integrated rate instead of constant pulse rate. ~200 lines.

4. **The gate test**: `tests/host/test_no_autorset_verb.exe` should print `VRB=[3,5]` after V35E. Currently `VRB=[-1,-1]`.

## How to start next session

1. Run `cd tests/host && mingw32-make diag && ROM=../../build/roms/Luminary099.bin ./test_ch010_writes.exe` — see baseline. Row 12 payload should be `60050` after V35E (lamps set) and rows 1-11 should still be `payload=0` (no digits).
2. Add output-channel tracing to `tests/host/test_ch010_writes.c` to log channels 5, 6, 11, 13, 14 during the V35E sequence. Identify what bits Luminary's setting.
3. Look up MAKEPLAY in `DISPLAY_INTERFACE_ROUTINES.agc` and trace what PLAYTEM4 / FLAGWRD4 / NBUSMASK gating checks need.
4. Extend `peripheral_stub_step` to respond appropriately — likely setting flagword bits or toggling specific channel 30/31/32/33 bits in response to AGC output channels.

## What NOT to do

- **No direct erasable pokes** to bypass Luminary state. Already tried (MASS, DAPBOOLS seed) — doesn't survive FRESH-START. The simulator must drive state via the engine's input channels and counter pulses, same path LM_Simulator uses on Pi/Linux.
- **No "just disable the alarm" shortcuts.** Each band-aid hides the next issue. The fix is real peripheral simulation.
- **Don't read erasable cells at offsets you haven't verified.** Today wasted hours on wrong-cell reads. Use `grep -nE 'SYMBOL[[:space:]]+(EQUALS|ERASE|=)' .../ERASABLE_ASSIGNMENTS.agc` to find the canonical definition, then look up the actual address in `/tmp/luminary.lst` (rebuilt via `cd third_party/virtualagc/yaYUL && ./yaYUL.exe ../Luminary099/MAIN.agc > /tmp/luminary.lst`).
- **Don't guess at addresses by counting `ERASE` directives.** Use yaYUL's listing — addresses appear in the format `bank,offset` (e.g. `20,2604`) which translates directly to `Fixed[bank][offset]` or `Erasable[bank][offset]`.

## Diagnostic infrastructure available

All built today, under `mingw32-make diag` (not in `run` so they print rather than assert):

| Test | Purpose |
|---|---|
| `test_z_histogram` | PC density across 2M cycles; finds hot loops. |
| `test_hotloop_disasm` | Dumps fixed-memory code around the hottest Z. |
| `test_restart_path` | Tracks GOJAM triggers + watchdog flag transitions. |
| `test_slots_correct` | Dumps all 8 executive slots with **correct** offsets (MPAC[0]=`0154+N*014`, LOC=`MPAC+8`, PRIORITY=`MPAC+11`). Use this as the authoritative pattern. |
| `test_slot0_origin` | Slot 0 state across boot timeline. |
| `test_no_autorset_verb` | Posts V35E and dumps all slots; the **decisive failure gate**. |
| `test_ch010_writes` | Dumps Luminary's ch010 row writes; proves Luminary IS responding to keypresses by setting lamp bits, just not digit rows. |
| `test_long_run` | Runs 20M cycles, confirms slot 0 doesn't free. |

## File map

| Path | Purpose |
|---|---|
| `components/peripheral_stub/peripheral_stub.c` | Real simulator: `_init` writes initial channels, `_step` runs at 100 Hz, `_tick` is the legacy hook (drives `_step` at 200 ms cadence from channel_router_on_routine). |
| `components/peripheral_stub/include/peripheral_stub.h` | API: `peripheral_stub_init`, `_step(state, dt_us)`, `_tick(state)`. |
| `main/app_main.c` | Calls `peripheral_stub_init()` after `agc_core_init`. (TODO: spawn dedicated 100 Hz FreeRTOS task calling `_step` directly.) |
| `tests/host/agc_harness.c` | `harness_boot()` calls `peripheral_stub_init()`. |
| `third_party/virtualagc/Contributed/LM_Simulator/lm_simulator.tcl` | Reference. Specifically `:570-577, 591-600, 879-893` for the channel writes. |
| `third_party/virtualagc/Luminary099/DISPLAY_INTERFACE_ROUTINES.agc:743-800` | MAKEPLAY routine — the thing slot 0 is stuck in. |
| `third_party/virtualagc/Luminary099/AOSTASK_AND_AOSJOB.agc` | 1/ACCS, where slot 0 used to be stuck before CDU pulses moved it forward. |
| `third_party/virtualagc/Luminary099/FLAGWORD_ASSIGNMENTS.agc` | Bit definitions for DAPBOOLS, FLAGWRD0-15. |
| `/tmp/luminary.lst` | yaYUL listing — symbol → bank,offset lookup. |

## Expected outcome

If Increment C lands a working simulator for channels 5/6 jet response + a minimal attitude model: slot 0 frees, DSPOUT runs, `test_no_autorset_verb` prints `VRB=[3,5]`, hardware DSKY shows VRB=35 after the curl V35E sequence.

The path beyond V0 (multiple programs running V37E63E etc.) requires more substantial dynamics simulation but is straightforward once the architecture works for V0.
