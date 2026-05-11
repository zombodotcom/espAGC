# Next session handoff — KEYRUPT1 is never dispatched on hardware

Last touched: 2026-05-11. All commits up to `fc0f406` pushed to `origin/main`.

## What is the actual problem (UPDATED 2026-05-11 PM)

**The previous framing was wrong.** Earlier sessions said *"KEYRUPT1 fires and dispatches to 04024 but CHARIN doesn't execute its first instruction."* The new hardware tracer (`CONFIG_AGC_TRACE_KEYRUPT1=y`, hook in `components/agc_core/io_callbacks.c::ChannelInput`) proves this is not what's happening.

**Actual finding from the trace:** during boot (1.3s–5.7s observed), the engine spends its time servicing **DOWNRUPT** (vector lead-in at `04040`, per `Luminary099/INTERRUPT_LEAD_INS.agc:75-78`) at a rate of roughly one full DOWNRUPT cycle every ~90ms. `InIsr=1` during each burst, `FBANK` varies across firings showing the interrupted code's bank context. The KEYRUPT1 lead-in (`04024..04027`, same file:60-63) **never appears in the trace** — not at boot, not after auto-RSET posts the keypress, not at any point.

So the bug isn't *"KEYRUPT1 dispatches to a bad CHARIN"*. The bug is **KEYRUPT1 doesn't dispatch at all on hardware.** That makes the prior `077615 bad CADR` observation in `test_executive_state.c` even more suspicious — it can't have come from KEYRUPT1→NOVAC→CHARIN if KEYRUPT1 never ran. Likely it was the residue of some other NOVAC caller, or a sampling artifact across host-vs-firmware timing.

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

## Hypotheses for why KEYRUPT1 never dispatches

The tracer doesn't yet log enough state to pick between these; that's the next step.

1. **`InterruptRequests[5]` set too late.** `channel_router_pump_input` (in `components/channel_router/channel_router.c`) sets `state->InterruptRequests[5] = 1` after `WriteIO(state, 015, code)`. It's called from `ChannelInput()` which yaAGC invokes *after* the engine's interrupt-dispatch decision for that cycle. So the request takes effect on the next `agc_engine()` call. By then, the engine has already started DOWNRUPT's body (`InIsr=1`), and KEYRUPT1 can't preempt. Result: KEYRUPT1's request sits there for the entire DOWNRUPT body (~50ms-worth of engine cycles). When DOWNRUPT's `RESUME` fires and clears `InIsr`, the engine *should* immediately dispatch KEYRUPT1 on the next cycle — but apparently doesn't.

2. **`RESUME` is clearing all `InterruptRequests[]`.** If yaAGC's RESUME implementation zeroes the whole array (or specifically index 5) after each ISR, our auto-RSET request gets nuked before it can fire. Need to read `agc_engine.c` RESUME handling and check.

3. **`AllowInterrupt` is stuck at 0.** Luminary's executive often does `INHINT` / `RELINT` pairs. If something prevents the relint, interrupts stay locked forever. Tracer doesn't log `AllowInterrupt` yet.

4. **DOWNRUPT body is re-requesting itself before exiting.** If DODOWNTM in `T4RUPT_PROGRAM.agc` (or equivalent) re-arms `InterruptRequests[8]` as a side effect, the engine never gets an idle moment for KEYRUPT1.

5. **Our `peripheral_stub_tick` or `channel_router_on_routine` is clearing `InterruptRequests[5]`.** Pure paranoia — verify by reading both for any erasable/state writes that touch `state->InterruptRequests`.

## Concrete next-session work

1. **Extend the tracer to a "dispatcher trace" mode.** New Kconfig `AGC_TRACE_DISPATCH` (or fold into existing flag). Hook moves to log on **every cycle** (not just within an address window): `InterruptRequests[1..10]`, `AllowInterrupt`, `InIsr`, `RuptLock`, `Z`. Then we can see whether `InterruptRequests[5]` ever becomes 1 and what state the engine is in when it does. Rate-limit to one log per ms or accept the flood for a short capture window — even at 100kHz, single-line UART formatting takes too long; suggest a ring-buffer-to-RAM mode flushed at the end of a fixed cycle count.

2. **Once `InterruptRequests[5]==1` is observed, watch `InIsr` transitions.** The cycle where `InIsr` goes 1→0 with `InterruptRequests[5]==1` is the moment of truth. If the very next cycle has `Z=04024`, dispatch works (hypothesis 1 was right, original framing was just timing-sensitive). If `Z` goes somewhere else and `InterruptRequests[5]` is still 1, hypothesis 2/3/5 alive.

3. **Read `third_party/virtualagc/yaAGC/agc_engine.c` interrupt dispatch in full.** Specifically the block that compares `InterruptRequests[]` against priority and dispatches. yaAGC has subtle rules (e.g. interrupts can only be taken between instruction boundaries, can be locked by certain flags). Lines mentioned in earlier session notes: `:2480-2600`. Compare what the trace shows against what yaAGC expects.

4. **If hypothesis 4 (DOWNRUPT self-requeue) lands**, find where DOWNRUPT is re-armed. Likely culprit: our integration's downlink handling is missing a step that yaAGC's reference desktop integration includes (since the desktop integration uses `SocketAPI.c`, not our `channel_router`, so downlink-channel writes there go to a TCP peer that ACKs them — our channel_router silently discards them).

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

The original handoff text below predates the dispatcher-trace finding above. Slot-0 CADR observations may have been artifacts. Trust the new framing.

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
