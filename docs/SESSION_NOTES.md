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
