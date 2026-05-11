# Next session handoff — trace KEYRUPT1 handler body, look for bad CHARIN dispatch

Last touched: 2026-05-11 (late). All commits up to `630f56a` pushed to `origin/main`.

## What is the actual problem (UPDATED AGAIN 2026-05-11 late)

**Two false starts behind us.** The trail of theories so far:

1. **First framing** (early sessions): *"KEYRUPT1 fires, NOVAC schedules CHARIN with bad CADR `077615`, CHARIN never runs."* Based on `test_executive_state.c` observations.
2. **Second framing** (earlier 2026-05-11): *"KEYRUPT1 never fires on hardware."* Based on dispatcher trace showing no `Z=04024` events. **This was a Monitor rate-suppression artifact** — single-cycle KEYRUPT1 events were getting dropped while DOWNRUPT's 4-cycle bursts came through.
3. **Current framing** (this session, late 2026-05-11): The full producer→consumer→dispatcher chain works correctly:
   ```
   chrouter: post: code=22 queued, head=1 tail=0
   chrouter: auto-RSET posted at boot (tick 16)
   chrouter: pump: pulled code=22 ir5=1 ch015=00022
   disp:    Z=04024 isr=1 AI=1 reqs[1..10]=0000000000
   keyrupt: Z=04024 ...  DXCH ARUPT
   keyrupt: Z=04025 ...  CAF KEYRPTBB
   keyrupt: Z=04026 ...  XCH BBANK
   keyrupt: Z=04027 ...  TCF KEYRUPT1 (-> handler in switched bank)
   ```
   KEYRUPT1 dispatches reproducibly. The 4-instruction lead-in executes cleanly. After `TCF KEYRUPT1` the PC jumps to the actual handler in switched-bank fixed memory (FBANK was `11000` octal at the moment of dispatch — that's bank 0o22 = bank 18 decimal in the upper-bank space, by the encoding `FBANK = (bank << 10)` so bank 0o22 → `0o22 << 10 = 0o22000` octal, but the trace shows `FB=11000` which decodes as bank `0o11` = 9, after super-bank... TBD).

   **The trace stops there.** Our current `KEYRUPT_LO/HI = 04024..04046` window doesn't cover the handler body. So we see the engine fall through `TCF KEYRUPT1` and... go somewhere we don't observe.

So the first framing (bug downstream of KEYRUPT1 lead-in) appears to be the right one after all. The `077615 bad CADR` may or may not be the actual symptom — needs re-verification once we can see the KEYRUPT1 body executing.

## What works (don't break)

- Boot to clean DSKY (host-side ERROR catches the boot NW transient).
- COMP ACTY blinks (Luminary's executive *is* executing instructions during the DOWNRUPT gaps).
- `peripheral_stub_tick` restoration of `ch030`/`ch033` baselines and IMODES30/IMODES33 fresh-start values.
- Host test suite: 15/15 passing.
- The replay-test decoder path (`test_replay_apollo11_launch`) is independently correct.
- Ring-buffer race in `channel_router_post_key` / `channel_router_pump_input` is now closed (commit `375db8d`, `portMUX_TYPE` critical section). The race was a real defect but not THE bug — disproven by the trace finding above.

## New hard evidence

From `CONFIG_AGC_TRACE_KEYRUPT1=y` build, UART captured at 115200 baud on COM7:

- ~50 DOWNRUPT lead-in entries logged across 1.3–5.7s (one every 60–120ms).
- Every entry: `Z=04040 FB=<varies> A=<varies> L=<varies> Q=<varies> isr=1 ec=0`. Then 04041, 04042, 04043 — exactly the four-instruction DOWNRUPT lead-in sequence.
- Zero entries with `Z` in `04024..04027` (the KEYRUPT1 lead-in window).
- Auto-RSET fires at tick 16 (~2.5s); `channel_router: auto-RSET posted at boot` line confirms `channel_router_post_key(DSKY_KEY_RSET)` ran. But no KEYRUPT1 trace entry follows it.

## Hypotheses for why CHARIN doesn't run after KEYRUPT1 dispatches

(Replacing the now-invalidated "never dispatches" section.)

Once `TCF KEYRUPT1` fires (last instruction of the lead-in at `04027`), the PC jumps to the handler body that lives in switched-bank fixed memory. We do NOT trace that range yet. The handler is:

```
KEYRUPT1   TS BANKRUPT       ; save BBANK
           XCH Q              ; save Q -> QRUPT
           TS QRUPT
           TC LODSAMPT        ; snapshot time
           CAF LOW5
           EXTEND
           RAND MNKEYIN       ; pull 5-bit keycode from ch015
KEYCOM     TS RUPTREG4
           CS FLAGWRD5
           MASK DSKYFBIT
           ADS FLAGWRD5
ACCEPTUP   CAF CHRPRIO        ; A = priority constant
           TC NOVAC           ; schedule CHARIN as a NEW JOB
           EBANK= DSPCOUNT
           2CADR CHARIN
           CA RUPTREG4
           INDEX LOCCTR
           TS MPAC
           TC RESUME
```

(`Luminary099/KEYRUPT,_UPRUPT.agc:39-59`.) Live hypotheses:

1. **TC LODSAMPT / RAND MNKEYIN reads the wrong value from ch015.** Our `channel_router_pump_input` writes the keycode via `WriteIO(state, 015, code & 037)` then sets `InterruptRequests[5]=1`. If `WriteIO`'s ch015 side-effect mutates the value before the handler reads it, RAND would see a wrong code. Verify: log `state->InputChannel[015]` at the moment of `RAND MNKEYIN` execution.

2. **NOVAC reads the 2CADR words at Q+0/Q+1 from the wrong bank.** This is the original `077615` hypothesis, now back on the table. Our trace already shows Q at the moment of dispatch (e.g. `Q=02471` for the second keypress observed). After `TC NOVAC` runs, Q should be `04041` (just past the TC). NOVAC's `INDEX Q; DCA 0` should then read `Fixed[2][041..042] = 034062, 056006`. If banking is off, it could read garbage. Verify: extend tracer window to capture the NOVAC body, not just the KEYRUPT1 lead-in.

3. **Slot allocation in NOVAC2 writes to a wrong PRIORITY index.** After NOVAC2 figures out which slot is free, it stores PRIO and CADR via `INDEX NEWJOB; TS PRIORITY` etc. If `NEWJOB` is wrong-banked or computed wrong, CADR lands in an unrelated cell. Verify: dump the entire PRIORITY array at the moment NOVAC2 completes.

4. **CHARIN's first instruction faults silently.** CHARIN at `PINBALL_GAME__BUTTONS_AND_LIGHTS.agc:475` starts `CAF ONE; XCH DSPLOCK; TS 21/22REG`. If `DSPLOCK` resolves to the wrong erasable cell, the executive's lock-tracking breaks immediately. Verify: trace through CHARIN's first ~10 instructions.

## Concrete next-session work

1. **Widen the KEYRUPT1 trace window** to cover the handler body, not just the 04024-04046 lead-in. The handler lives in a switched bank — we know FBANK at the moment of dispatch (FB=11000 octal observed). Two approaches:
   - Hardcode the handler address range. `KEYRUPT1` (the label) is in `KEYRUPT,_UPRUPT.agc` after `SETLOC KEYRUPT`. The actual fixed address depends on what `KEYRUPT` is defined as — derive from `MAIN.agc.html` or by re-running yaYUL with listing.
   - Or: latch the trace into "follow ISR" mode the moment we see `Z=04024 isr=1`, then dump every instruction until `InIsr` transitions to 0. This is simpler but produces 100-500 log lines per keypress. Acceptable for a focused diagnostic — keypress rate is low.

2. **Make the keyrupt log include `RegA`/`RegL`/`InputChannel[015]` at every step.** When the handler executes `RAND MNKEYIN`, we want to see the masked keycode in A right after. If A reads as 0 (or anything other than 022 octal for an RSET press), the keycode path is broken before NOVAC.

3. **After NOVAC's `DCA 0` instruction, log A:L (the 2CADR words).** Compare against the known-good ROM values `Fixed[2][041]=034062, Fixed[2][042]=056006` (per `test_cadr_resolution`). Mismatch → engine fetch bug (hypothesis 2). Match → slot allocation bug (hypothesis 3) is the next thing to investigate.

4. **Spurious-keypress note**: the second keypress observed in this session was `code=07`, NOT posted by anything in our code. Likely the XPT2046 touch driver registering noise. Worth muting (a debounce / sanity threshold in `components/dsky_input/touch.c` or wherever the touch task posts keys) so we're not chasing ghost dispatches in future traces. Not the main bug, but adds noise to UART captures.

5. **Cleanup**: once a fix lands, decide whether to keep `CONFIG_AGC_TRACE_KEYRUPT1` (probably yes, as `=n` it's free) and `test_cadr_resolution.c` (probably yes, as a standalone diagnostic). Add a real assertion-bearing host test for V35E lamp test once it works.

## Files to read first next session

- `components/agc_core/io_callbacks.c` — current tracer is here (`#ifdef CONFIG_AGC_TRACE_KEYRUPT1`). Next pass extends it.
- `components/channel_router/channel_router.c` — `channel_router_pump_input` (lines ~254-275). The keypress→`InterruptRequests[5]=1` path.
- `third_party/virtualagc/yaAGC/agc_engine.c:2480-2600` — interrupt dispatch logic. Did not re-verify line numbers; grep for `InterruptRequests` and `InIsr`.
- `third_party/virtualagc/Luminary099/INTERRUPT_LEAD_INS.agc` — confirms the lead-in addresses (`04024 KEYRUPT1`, `04040 DOWNRUPT`, etc.).
- `third_party/virtualagc/Luminary099/T4RUPT_PROGRAM.agc` — DODOWNTM lives here (the body of DOWNRUPT). Look for any re-arming of DOWNRUPT or related interrupt channels.

## What NOT to do

- **Don't add more host-side fakes** in `peripheral_stub_tick`. The `FAILREG==01107` carve-out is the limit. Anything more makes the dongle "look right" while hiding the actual broken dispatch.
- **Don't auto-press more keys from C.** The auto-RSET hack was the line — anything beyond that is lying about what the AGC is doing.
- **Don't switch ROMs to avoid this.** TicTacToe would probably work (no executive complexity), but the goal is Luminary running correctly.
- **Don't claim "15/15 tests passing" as success.** `test_lamp_test` and `test_p00_select` have zero assertions about the behaviors they're named after. Real verification means asserting the lamp test actually lit segments, P00 actually shows `00` in PROG digits. Real tests are also still needed for the dispatch fix once we have one.

## Expected outcome

After the next focused session: either (a) the dispatcher trace identifies why `InterruptRequests[5]` doesn't trigger KEYRUPT1, leading to a small fix (1-20 lines, likely in `channel_router.c` or `io_callbacks.c`), or (b) we conclude the issue is in vendored `agc_engine.c` and need to either patch it (last resort) or change our integration to match what upstream `SocketAPI.c` does that we don't.

If (a), then we still owe real assertion-bearing tests for `test_lamp_test` and `test_p00_select` to replace the assertion-free versions currently in tree. If (b), update this doc and plan for a multi-day intercept.

## Original framing (kept for history)

The text below predates this session's findings. It was correct that something downstream of KEYRUPT1 is broken; it was wrong only about whether KEYRUPT1 itself fires. Trust the new framing at the top of the document. Slot-0 CADR `077615` observation may or may not still hold — re-verify with the widened tracer.

---

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
