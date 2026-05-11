# Next session handoff — KEYRUPT1 → CHARIN dispatch is broken

Last touched: 2026-05-11. All commits up to `483aed6` pushed to `origin/main`.

## What is the actual problem

**The DSKY is input-deaf.** Web UI buttons (V35E lamp test, V37E00E, RSET, etc.) reach our `channel_router` correctly. The keycode is written to `InputChannel[015]` via `WriteIO`. `KEYRUPT1` (interrupt index 5) fires and the engine dispatches to address `04024`. But Luminary's `CHARIN` job — which `KEYRUPT1` schedules via `NOVAC` — never actually executes its first instruction.

Hardware confirms this: UART log from 2026-05-11 flash shows boot, auto-RSET clears PROG ALARM (lamp goes from `payload=0400` to `0000`), COMP ACTY blinks, but a `V35E` sequence produces no lamp-test pattern in ch010 row writes. Same as the host tests.

## What works (don't break)

- Boot to clean DSKY (host-side ERROR catches the boot NW transient).
- COMP ACTY blink (Luminary's executive *is* executing instructions).
- `peripheral_stub_tick` restoration of `ch030`/`ch033` baselines and IMODES30/IMODES33 fresh-start values.
- Host test suite: 15/15 passing.
- The replay-test decoder path (`test_replay_apollo11_launch`) is independently correct.

## Hard evidence, with file:line

Run `tests/host/test_executive_state.c` to reproduce the diagnostic. Key observations from the latest run (commit `483aed6`):

1. **Cycle 125k (before auto-RSET fires at tick 16 ≈ 131k):** `slot0 PRIO=030110 CADR=000000`. Slot 0 already has `CHRPRIO+FAKEPRET` priority but no `CADR` — something pre-allocated it.
2. **Cycle 135k (after auto-RSET):** `slot0 PRIO=030110 CADR=077615`, `LOC=000002`, `BANKSET=010006`. Auto-RSET's NOVAC populated the slot but the encoded CADR `077615` decodes to bank `037` offset `01615` which is **not a valid code address** (bank 37 fixed memory starts at offset `02000`).
3. **Cycle 230k (95k later):** `slot0` unchanged. Slot 0 never frees (never goes to `077777` = -0). `NEWJOB=000000` throughout.
4. **`DSPLOCK=000000` across every snapshot.** CHARIN's literal first instruction (`PINBALL_GAME__BUTTONS_AND_LIGHTS.agc:475-477`) is `CAF ONE; XCH DSPLOCK; TS 21/22REG` which sets `DSPLOCK` to 1. **CHARIN never reaches that instruction.**

So: the executive *thinks* it's running CHARIN (slot is marked allocated, priority is correct, CADR field is non-zero) but the actual CHARIN code never runs. Yet Luminary's engine *is* executing instructions across multiple banks (`FBANK` changes between snapshots, `RegZ` cycles through real code addresses).

## Three hypotheses for the bug (untested)

1. **Bad CADR encoding.** `077615` doesn't look like a valid `2CADR`. Either NOVAC reads the wrong words after the `TC NOVAC` call, OR our engine's instruction fetch for the `2CADR CHARIN` directive computes a different value than upstream yaAGC. Test: read `Erasable[N][CADR_OFFSET]` after KEYRUPT1 exits but before slot-0 takes off, and verify it matches the computed encoding of `2CADR CHARIN` (`EBANK= DSPCOUNT` from `KEYRUPT,_UPRUPT.agc:53`).

2. **Interrupt/job-state race.** Our `channel_router_pump_input` writes `InputChannel[015] = code` and `InterruptRequests[5] = 1` inside the engine's `ChannelInput` callback, which is itself called from `agc_engine()`. The upstream `ringbuffer_api.c:148-149` does the same. But the engine's interrupt dispatch checks `AllowInterrupt`, `InIsr`, `ExtraCode`, `PendFlag` — and may discard the interrupt request under conditions we hit. Test: log `InterruptRequests[5]` transitions cycle-by-cycle around the keypress arrival.

3. **Engine's instruction fetch is broken for our integration.** Our `agc_init.c` initializes `Erasable[0][RegZ] = 04000` and clears all `InputChannel[]`. yaAGC's `agc_engine_init.c:255-258` does effectively the same, except for the `036377` vs `037777` ch030 difference (which is intentional). Test: diff our init vs upstream agc_engine_init.c side-by-side and check for any field we missed (e.g., `OutputChannel7`, `DskyChannel163`, parities).

The slot-0 stuck pattern is consistent across runs and is reproducible in the host test, so it's deterministic. That helps.

## Concrete next-session work, in priority order

1. **Verify the CADR hypothesis (hypothesis 1) first** — it's cheapest to test. Steps:
   - Look up `2CADR CHARIN`'s assembled bytes in `MAIN.agc.html` or by re-running yaYUL with listing.
   - Step the engine cycle-by-cycle through the KEYRUPT1 entry until the `DCA 0` instruction inside `NOVAC` (`EXECUTIVE.agc:48`) runs.
   - Dump `A`, `L`, `Q`, and the two words at `Q` to verify NOVAC reads the 2CADR correctly.
   - If the value matches a valid CHARIN address, hypothesis 1 is wrong and move on.

2. **If hypothesis 1 fails, instrument `agc_engine.c` line 2480-2600 (interrupt dispatch)** to log every time an interrupt is taken and every time it's skipped because of `AllowInterrupt`/`InIsr`/etc. Compare with what's happening at the keypress moment.

3. **Stop pretending we can stub our way out of this.** If the bug is in our `agc_init.c` or `io_callbacks.c`, port a known-working yaAGC standalone harness (`Run-LM.sh` setup) onto a host and run the same keypress sequence through it. If keypresses work there, diff their setup against ours.

## What NOT to do next session

- **Don't add more host-side fakes** to `peripheral_stub_tick`. The existing `FAILREG == 01107` check is already a hack. Anything more makes the dongle "look right" while hiding worse breakage.
- **Don't switch ROMs to avoid this.** TicTacToe would probably work (no executive complexity), but the goal is Luminary running correctly. Switching ROMs is option C from the original 4-path plan, deferred for good reason.
- **Don't auto-press more keys from C.** The auto-RSET hack was reasonable for clearing the boot lamp; auto-pressing V35E or similar would be lying about what the AGC is doing.
- **Don't claim "15/15 tests passing" as success.** Two of those tests (`test_lamp_test`, `test_p00_select`) have **zero assertions about the behavior they're named after** — they just step the engine and call `PASS()`. Real verification means asserting the lamp test actually lit segments, P00 actually shows `00` in PROG digits, etc.

## Files to read first next session

- `tests/host/test_executive_state.c` — the diagnostic that produced the evidence above.
- `docs/SESSION_NOTES.md` (last three sections) — full chronological investigation log.
- `third_party/virtualagc/Luminary099/EXECUTIVE.agc` lines 35-220 (NOVAC, FINDVAC, CORFOUND, SETLOC) — the scheduler.
- `third_party/virtualagc/Luminary099/KEYRUPT,_UPRUPT.agc` lines 35-60 (KEYRUPT1's NOVAC call).
- `third_party/virtualagc/Luminary099/PINBALL_GAME__BUTTONS_AND_LIGHTS.agc` lines 475-525 (CHARIN entry + dispatch table).
- `third_party/virtualagc/yaAGC/agc_engine.c` lines 2480-2600 (interrupt dispatch in the engine).
- `third_party/virtualagc/yaAGC/ringbuffer_api.c` lines 130-180 (upstream keypress delivery pattern — what we're trying to match).

## Expected outcome

After the next focused session: either (a) the bug is identified and a 5-50 line fix makes V35E light all segments and V37E00E select P00, or (b) we've ruled out the three hypotheses and need to move to the full LM_Simulator path (option A from the original plan).

If (a), then write real assertion-bearing host tests for `test_lamp_test` and `test_p00_select` to replace the assertion-free versions currently in tree. If (b), update this doc and the plan, and budget multi-day work for option A.
