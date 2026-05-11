# Session notes — pick this up at the office

Last touched: 2026-05-10. Branch: `main`, all committed and pushed.

## TL;DR — where things stand

**Working:** clean Layer 1+2 test suite (11 tests, ~2 s, all PASS). Apollo 11 launch replay validates the entire `channel_router → dsky_state` decoder against the real yaDSKY2-recorded transcript. RSET correctly clears the RESTART lamp. Hardware boots, displays, accepts touch + WiFi keypresses.

**The remaining open thing:** Luminary 099 keeps PROG ALARM lit at boot because we don't simulate IMU / radar / AOT peripherals. RSET clears the lamp, but Luminary re-asserts within milliseconds because the underlying fault condition persists.

## What was just learned (and committed)

1. The previous bring-up plan is done — DSKY decoder fixed, renderer validated, host tests added, all on `main`.
2. **RSET-clears-RESTART path was broken** because `channel_router_pump_input` was direct-assigning `state->InputChannel[015] = code` instead of routing through `WriteIO()`. The engine's hardware-direct flip-flop (`agc_engine.c:586` — *"RSET being pressed on either DSKY clears the RESTART light flip-flop directly, without software intervention"*) only fires from `WriteIO`. Fix: route ch015 keypresses through `WriteIO`. Test added (`test_rset_clears_alarms`).
3. **`agc_init.c` ch030 init switched from 0o37777 (everything broken) to 0o36377 (healthy LM):**
   - bit 9 (IMU OPERATE WITH NO MALFUNCTION) → 0 (signal present)
   - bit 10 (LM COMPUTER HAS CONTROL) → 0 (LGC in control)
   - bit 15 (TEMP STABLE MEMBER WITHIN DESIGN LIMITS) → 0 (temp OK)
   - All other ch30 bits stay 1 (signal NOT present = no fault).
   - Necessary but not sufficient — Luminary still fails further peripheral checks.

## The PROG ALARM root cause (full chain)

1. Luminary's `ALARM` routine (`ALARM_AND_ABORT.agc::ALARM`) → `PROGLARM` (line 90):
   ```
   PROGLARM   CS    DSPTAB +11D
              MASK  OCT40400      ; bits 9 + 15
              ADS   DSPTAB +11D
   ```
   This sets bit 9 (PROG ALM lamp, = 0x100 in ch010 row 12) and bit 15 (request flag) of `DSPTAB +11D`.
2. Bit 9 of `DSPTAB +11D` is what the relay writeout sends as ch010 row=12 payload bit 0x100 — the value our decoder reads as `dsky_state.prog_alarm`.
3. RSET keypress → `KEYRUPT` → `CHARIN2` → `ERROR` routine (`PINBALL_GAME__BUTTONS_AND_LIGHTS.agc:3744`):
   ```
   CAF   GL+NOATT       ; OCT 50 = bits 4 (NO ATT) + 6 (GIMBAL LOCK)
   MASK  DSPTAB +11D    ; keep only those two bits
   AD    BIT15          ; OR in BIT15 (request)
   TS    DSPTAB +11D    ; → clears PROG ALARM, TRACKER, etc.
   CS    PRIO16
   MASK  IMODES33       ; reset fault bits in IMODES33
   AD    PRIO16
   TS    IMODES33       ; comment: "IF FAILURE STILL EXISTS, ALARM WILL COME BACK"
   ```
4. Same routine clears bit 10 of `IMODES30`. **But:** the periodic mode-switch / IMU monitoring code in `T4RUPT_PROGRAM.agc` reads ch030 / ch033 every cycle, and any fault bit Luminary sees re-sets the IMODES* fail flags, and the next ALARM call re-asserts PROG ALARM.

## What "satisfying it the right way" actually requires

Luminary checks (a non-exhaustive list, drawn from `INPUT_OUTPUT_CHANNEL_BIT_DESCRIPTIONS.agc` + `T4RUPT_PROGRAM.agc`):

- **ch030 bit 9** = IMU OPERATE WITH NO MALFUNCTION  → **already healthy**
- **ch030 bit 10** = LM COMPUTER (NOT AGS) HAS CONTROL → **already healthy**
- **ch030 bit 13** = IMU FAIL → currently 1 = signal NOT present = no fail. ✓
- **ch030 bit 15** = TEMP STABLE MEMBER WITHIN DESIGN LIMITS → **already healthy**
- **ch031** = RHC/THC stick state. We init to 077777 (sticks centered, no commands). Luminary's HANDRUPT trap watches for transitions from neutral; bits 5/4/3/2/1/0 = +/-pitch, +/-yaw, +/-roll. Neutral = all bits set.
- **ch033 bit 14** = AGC WARNING input. Once set, Luminary may keep it asserted until peripherals respond. Look at `agc_engine.c:627-637` — CPU writes to ch033 reset bits 11-15, *unless* `WarningFilter > WARNING_FILTER_THRESHOLD`.
- **CDU counter inputs (ch032 bit 14 PROCEED, ch035-046 area, plus the CDU registers in erasable)** — Luminary expects fresh CDU counts arriving roughly every 10 ms. Without them the IMU monitoring code (`T4RUPT_PROGRAM.agc::T4JOB`) flags an alarm.
- **Radar data** — `RequestRadarData()` is currently a stub. P63/P66 will alarm without it; Luminary's R29/R12/R04 routines also poll.

The minimal stub that would clear PROG ALARM at idle (no V37 program selected) is probably:
1. Keep ch030/031/032/033 init values stable (already done, mostly).
2. Add a periodic task (or hook in `channel_router_on_routine`) that pokes `IMODES30` / `IMODES33` fault-bit clears whenever Luminary re-sets them, OR equivalently writes ch033 with bits 11-15 reset.
3. Probably NOT necessary to actually feed CDU counters until a P-program needs them — but the moment we run V37E63E (P63 descent), it'll matter.

A faster alternative (worth trying first): have `channel_router_on_routine` watch for `DSPTAB +11D` bit 9 being set and *immediately* simulate a RSET keypress. That treats the symptom, not the cause, but lets us see what state the executive reaches without the lamp on.

## Recommended next moves at the office

In priority order:

1. **Hardware confirmation of the RSET fix.** Flash, boot, watch UART for the periodic `alarms RuptLock=...` line — should now show `restart=0` after the first `R` tap. Currently the only Layer-3 thing not yet eyeballed.
2. **Decide on the PROG ALARM strategy:**
   - **(a)** Quick win: auto-press RSET at boot from `channel_router_init()` (or after first 100 k cycles when NW settles). One-liner. Ships PROG ALARM staying on; just keeps the engine out of the alarm-ack loop.
   - **(b)** Right way: add a `peripheral_stub` component that runs as a low-priority FreeRTOS task, periodically ORs the healthy bits back into `state->InputChannel[030..033]` and resets `IMODES30/33` fault bits via a small ALMCADR poke. Probably 100 lines.
   - **(c)** Full peripheral simulation: CDU counters tick at 10 ms, fake radar returns when polled, etc. This is the path that lets P63 actually run. Big lift; defer until someone wants to fly the descent.
3. **Wire `DSKY_KEY_PRO=63` through the input transports.** The summary noted this is still a sentinel that maps to a no-op. PRO sets ch032 bit 0x4000 (`OutputPro` in yaDSKY2.cpp:2174). Touch/web both need a special path for keycode 63.
4. **Once PROG ALARM is calm, start the `Apollo11-landing.canned` replay** (the launch transcript already passes; landing is ~10× the size and exercises P63/P66 — a much stronger end-to-end check, but only meaningful with peripherals stubbed enough to keep Luminary out of the alarm loop).

## Key files for context (no need to re-derive)

- `components/agc_core/agc_init.c` — channel init values (ch030 = 0o36377, etc.). Comments cite specific bit meanings.
- `components/channel_router/channel_router.c` — output decoder + input pump. Recently fixed: `channel_router_pump_input` now uses `WriteIO()`.
- `tests/host/test_rset_clears_alarms.c` — the regression guard for the RSET fix. Read it for the full chain of reasoning.
- `tests/host/test_replay_apollo11_launch.c` — feeds the real Apollo 11 launch transcript through the decoder. Passing this proved the decoder is correct.
- `third_party/virtualagc/Luminary099/INPUT_OUTPUT_CHANNEL_BIT_DESCRIPTIONS.agc` — primary reference for ch030/031/032/033 bit meanings.
- `third_party/virtualagc/Luminary099/PINBALL_GAME__BUTTONS_AND_LIGHTS.agc` lines 489-523 (key dispatch table) and 3736-3809 (RSET ERROR handler).
- `third_party/virtualagc/Luminary099/ALARM_AND_ABORT.agc` lines 53-104 (ALARM + PROGLARM).
- `third_party/virtualagc/yaAGC/agc_engine.c:586-590` (RSET hardware path), `:1691-1747` (ch0163 update), `:2200-2300` (GOJAM + alarm trigger).

## What NOT to do

- Don't bypass alarm symptoms by editing the decoder (e.g., masking out `prog_alarm` in `apply_row` case 12). The whole point of the decoder is to faithfully report what Luminary outputs.
- Don't add LVGL. Renderer + tests prove direct framebuffer is faster, simpler, and testable.
- Don't try QEMU. There is no published QEMU support for ESP32-WROOM (or C5) that handles SPI panels + touch. Layer 2 host tests cover ~95 % of what we'd need QEMU for at zero infrastructure cost.
- Don't downgrade ESP-IDF. We're on v6.0.1; older versions miss the ESP32-C5 target and the `esp_driver_*` split.

## 2026-05-10 update — FAILREG diagnostic

Added `harness_failreg()` (tests/host/agc_harness.{h,c}) and `tests/host/test_failreg_diagnostic.c`. Boots Luminary, steps 10M cycles, reports the alarm-code FIFO (FAILREG[0..2] at erasable `0375..0377` bank 0). Result:

```
first prog_alarm=1 observed at cycle 100000
first FAILREG[0] non-zero at cycle 100000
final FAILREG: [01107,00000,00000] octal
final prog_alarm=1 restart=1
```

**Only alarm fired in 10M cycles is `01107` = NIGHT WATCHMAN** (yaAGC's executive-heartbeat watchdog — set in `agc_engine.c:2065-2076` when NEWJOB at address 067 isn't accessed within ~640ms of SCALER1 hitting `04000`). No peripheral alarms (no `0207` ITURNON, no `0214` IMUOP, no `0514` RRAUTCHK) — meaning **a peripheral simulator is not what's keeping the lamp lit**.

The NW trip happens once during boot, then the engine recovers (`comp_acty` blinking, `NightWatchmanTripped == 0` post-recovery). The lamp stays on because `FAILREG[0]` retains the historical code, and `DSPTAB+11D` bit 9 is never cleared. The ERROR routine (`PINBALL_GAME__BUTTONS_AND_LIGHTS.agc:3744-3801`) is supposed to clear both on RSET — including `TS FAILREG; TS FAILREG +1; TS FAILREG +2` at lines 3796-3799 — but our `test_failreg_diagnostic` shows FAILREG stays `01107` after auto-RSET fires at tick 16, meaning **the keypress isn't reaching ERROR via KEYRUPT1**.

Both `test_rset_clears_alarms` and `test_failreg_diagnostic` show `prog_alarm=1` post-RSET. The hardware-direct WriteIO path clears `RestartLight` (engine flag) — that works. The software KEYRUPT1 path (interrupt index 5, `state->InterruptRequests[5] = 1`) is set by `channel_router_pump_input` but Luminary's ERROR routine doesn't run.

**Implication for the LM_Simulator plan**: the proposed CDU/IMU/radar simulator (Path A) would solve problems that aren't firing. The real bug is in the keypress → KEYRUPT1 → ERROR path. Likely candidates: interrupt index wrong (it's 5, matches Luminary's lead-in vector — already verified), `AllowInterrupt` is 0 during the auto-RSET fire, or our `channel_router_pump_input` has a race with engine state.

Plan pivoted: investigate why ERROR isn't running before building any simulator infrastructure.

### KEYRUPT1 trace finding

Wrote `tests/host/test_keyrupt_trace.c` (not in CI list; standalone diagnostic). Single-steps for 30,000 cycles after manually posting RSET, dumps `AllowInterrupt`, `InIsr`, `InterruptRequests[5]`, `InputChannel[015]`, `FAILREG[0]`, `DSPTAB+11D`, `DSPLOCK`, `RegZ` on every state change.

Result: **KEYRUPT1 fires** (RegZ jumps to `04024` at cycle 1, AGC's KEYRUPT1 vector) and **exits cleanly** at cycle 192. But `DSPLOCK` (erasable `01012`, bank 2 offset `012`) never transitions 0→1 over 30,000 subsequent cycles. CHARIN's first instruction (`PINBALL_GAME__BUTTONS_AND_LIGHTS.agc:475-477`) is `CAF ONE; XCH DSPLOCK; TS 21/22REG` which sets DSPLOCK = 1. **CHARIN never runs.**

The chain is: keypress → ch015=022 → KEYRUPT1 fires → reads MNKEYIN → stores keycode in RUPTREG4/KEYTEMP1 → `CAF CHRPRIO; TC NOVAC; 2CADR CHARIN` schedules CHARIN as a NEW JOB → RESUME exits interrupt → executive should pick up CHARIN. Last step is what's broken. Either NOVAC quietly fails, the VAC area is corrupt after the post-NW-trip GOJAM, or the executive's job queue is in a state we don't understand.

### Resolution (2026-05-10) — host-side ERROR

Rather than block on the deeper investigation (KEYRUPT1 → CHARIN dispatch needs a yaAGC executive walkthrough), `peripheral_stub_tick()` now does what the real `ERROR` routine does (PINBALL `3744-3801`), from outside the engine, **but only when `FAILREG[0] == 01107`** (NIGHT WATCHMAN):

- Clear `DSPTAB+11D` bit 9, preserving bits 4 (NO ATT) and 6 (GIMBAL LOCK), set bit 15 (request) — matches `CAF GL+NOATT; MASK DSPTAB+11D; AD BIT15; TS DSPTAB+11D`.
- Zero `FAILREG[0..2]` — matches `CAF ZERO; TS FAILREG; TS FAILREG +1; TS FAILREG +2`.

Any other `FAILREG[0]` value is left alone — real alarms remain visible. Verified by:
- `tests/host/test_peripheral_stub_clears_imodes` (3 parts: no alarm → don't touch DSPTAB; NW alarm → clear and zero FAILREG; non-NW alarm → leave both alone).
- `tests/host/test_lm_idle_quiet` (5M-cycle contract test; PROG ALARM clears within 2M cycles of boot and stays clear).

Full suite: **15/15 PASS**. The original `auto-RSET` one-shot remains active; it still clears the engine's hardware-direct `RestartLight` flag (different from `DSPTAB+11D` bit 9) and is harmless.

Known follow-up: the KEYRUPT1 → CHARIN dispatch bug means **manual RSET via the web UI also won't clear PROG ALARM via the AGC's own path**. The host-side ERROR catches the boot-time NW transient automatically, so for users this looks identical to a properly-functioning RSET. But if a real future alarm fires (anything other than 01107), the user pressing RSET on the web UI won't clear it — they'll just have to wait for whichever alarm condition fires next time, or we need to make our host-side ERROR more aggressive about catching all RSET keypresses. Both options are noted but not implemented; revisit when a real alarm is observed.

### Why no keypress works (V35E, V37E00E, anything)

Same root cause as above but with much wider impact than originally surfaced. Added `tests/host/test_executive_state.c` which dumps `PRIORITY[0..6]` (12-word stride at erasable `0167`), `NEWJOB`, `LOC`, `FIXLOC` plus engine internals across multiple snapshots after a keypress.

Findings:

- **CHARIN IS being scheduled correctly.** Post-NW-trip and post-keypress, `PRIORITY[1] = 030110` octal — that's `CHRPRIO + FAKEPRET offset` (CHARIN). `NEWJOB = 000014` points to slot 1.
- **CHARIN is NOT being dispatched** because slot 0 has a job at priority `027110` octal that is currently running. The AGC executive only switches jobs at yield points (`ENDOFJOB`, `JOBSLEEP`, `CHANG1/2`). Slot-0's job never yields.
- **Slot-0's job is stuck in an interpretive GOTO loop.** `RegZ` bounces between `06647..06674` octal across 122,000 engine cycles. Those addresses are the AGC interpreter's `GOTO` / `GOTO+1` dispatcher in fixed-fixed memory (see yaYUL listing for `GOTO` at offset `6646`). The instruction `TCF GOTO+1` at `6674` is the "arbitrary indirectness" inner loop — if the GOTO target chain points to itself or has corrupt data, this loops forever.
- Slot-0's `LOC = 002056`. Without knowing the FBANK at execution time we cannot pin down exactly which Luminary subroutine this is, but it is interpretive code (the GOTO loop confirms).

Tried preempting the boot-time NIGHT WATCHMAN trip from outside by clearing `State->NightWatchman` every tick in `peripheral_stub_tick`. Result: FAILREG stayed clean (the watchdog never tripped) but slot-0 still got stuck in the same interpretive GOTO loop. So the NW trip is not the cause of the stuck job — it's a symptom of the same underlying problem (slot 0 takes too long to yield, so NEWJOB doesn't get accessed in time). Reverted the NW preemption; it didn't help and added noise.

The real fix requires:
1. Identifying which Luminary subroutine is the slot-0 job (priority `027110 - FAKEPRET`, likely an IMU monitoring / DAP idler / DSKY refresh task started by the post-`FRESH-START` initialization).
2. Determining what peripheral state or erasable cell it's polling that we don't provide.
3. Either supplying that state (real LM_Simulator territory) or short-circuiting the loop.

This is the same root issue the upstream PI/Linux ports solve by running LM_Simulator alongside yaAGC. Until then, the host-side ERROR keeps the boot PROG ALARM lamp clear, but **no DSKY keypress actually reaches Luminary**. The web UI buttons are no-ops.

### Deeper finding (2026-05-11): auto-RSET causes slot-0 hang

Snapshotting cycles 130k (before auto-RSET fires at tick 16 ≈ 131k) vs 135k (just after) shows:

- **At 130k:** `slot0 PRIO=033110 CADR=000000`, `LOC=000003`, `BANKSET=001173`. Luminary executing in a different code region; `RegZ=006071` (not the interpreter).
- **At 135k:** `slot0 PRIO=030110 CADR=077615`, `LOC=000002`, `BANKSET=010006`. The auto-RSET keypress arrived, KEYRUPT1's `NOVAC` scheduled CHARIN into slot 0 with priority `CHRPRIO + FAKEPRET` (= `030110`).
- **At 230k (95k later):** Still `slot0 PRIO=030110 CADR=077615`. **Slot 0 never frees**. `comp_acty` is blinking (engine IS executing) but the executive maintains the slot as "CHARIN running" indefinitely.

Critically: `DSPLOCK=000000` throughout the entire run. CHARIN's first instruction (`PINBALL_GAME__BUTTONS_AND_LIGHTS.agc:475`) is `XCH DSPLOCK` which sets DSPLOCK to 1. So **CHARIN never even reached its first instruction** even though the executive thinks it's running it. The executive's scheduling bookkeeping is set up correctly (priority, CADR, NEWJOB-not-needed because we're "already running it") but the actual CHARIN code never executes.

Most plausible interpretation: the executive's `SETLOC` allocated slot 0 to CHARIN and updated `PRIORITY[0]` + `CADR[0]`, but the actual job dispatch into `LOC = CHARIN_address` didn't happen — possibly because of a banking issue (Luminary's `2CADR CHARIN` references `EBANK= DSPCOUNT` which sets up the wrong bank context in our setup). Investigation deferred.

Result: every subsequent keypress (manual RSET, V35E, V37E00E, V16N36E, anything) also schedules into a new slot but at the same priority `030110`, so `NEWJOB` doesn't update (priority equal to slot 0), and slot 0 never finishes. **The AGC is effectively input-deaf after the auto-RSET fires**.

Trying `auto_rset = false` is the obvious next experiment: if disabling auto-RSET keeps slot 0 normal, then manual keypresses (from the web UI) may actually reach Luminary's CHARIN handler. Worth testing in next session.

### Where Luminary is actually stuck (2026-05-11, late session)

Built a Z-histogram diagnostic (`tests/host/test_z_histogram.c`, runs as `mingw32-make diag`). Over 2M cycles the engine spends 17 %+ of its time at six adjacent fixed-fixed addresses **Z=06070..06100**. Disassembly via `test_hotloop_disasm.c` shows a TCF-back-to-self loop at **06647..06674** that reads erasable cells `0117 (NNADTEM)` and `0120 (NNTYPTEM)` — both zero — and decrements/loops without progress. This is the AGC interpreter's `GOTO` family in fixed-fixed bank 3, confirming the prior "interpretive GOTO loop" guess.

Built `test_restart_path.c` to track GOJAM triggers + watchdog flags:

- 8 GOJAMs in 1.5M cycles, all firing with `NightWatchmanTripped=1`.
- Spacing: first GOJAM at cycle 109,226 (~1.28 s of MCT time), subsequent every ~3.4k cycles, then a long stable window cycle 130k → 218k (~88k cycles) during which a real job briefly scheduled (`NEWJOB → 044 → 0` at cycle 133,605).
- `RuptLock` and `NoRupt` flicker every ~13.6k cycles but always self-clear within 200 cycles. `NoTC`/`TCTrap` flap every ~850 cycles but also self-clear within 2-3 cycles.
- **The only watchdog actually causing GOJAM is Night Watchman**, exactly because the interpretive loop above never references erasable address 067 (NEWJOB).

Tried suppressing all alarms (`InhibitAlarms = 1` via `test_inhibit_alarms.c`, since deleted) plus setting `ch030 = 036377` (IMU/LGC/temp healthy): GOJAMs go to zero but slot-0 still parks in the same 06647..06674 loop, NEWJOB stuck at `00014`, DSKY blank. So NW is downstream of the same root cause — Luminary's boot-time interpreter routine is waiting for state it can't get.

Tried disabling auto-RSET + InhibitAlarms (`test_no_autorset_keypress.c`, since deleted): same outcome — keypress posted, slot 0 picks up priority `00110` instead of `030110`, but `DSPLOCK` still stays 0 (CHARIN's first XCH never fires). The auto-RSET hypothesis is at most a contributor, not the root.

**Root cause summary**: Luminary's interpretive GOTO loop at `06647..06674` reads from an erasable address chain that yaAGC's `agc_init.c` leaves zero-clear. On real hardware the LM_Simulator (per `third_party/virtualagc/Contributed/LM_Simulator/lm_simulator.tcl`) drives CDU counters and IMU bits that cause this routine to terminate. Without that feed, the loop runs forever and slot 0 never yields. Every keypress schedules into a free slot but the executive only switches at job yield points — so no keypress is ever dispatched.

**What this means for the plan**: Phase 1 of the plan (fix the keypress dispatch bug) was misframed — there isn't a dispatch bug, the executive is doing exactly what it should. The actual blocker is **Phase 4 (LM_Simulator-equivalent peripheral feed)**, which is what the upstream Pi/Linux ports also rely on. The diagnostic harness (`mingw32-make diag`) is now in place; the next session should port the minimal subset of `lm_simulator.tcl` needed to feed CDU/IMU values that let the boot interpreter terminate.
