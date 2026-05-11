# Next session handoff — FBANK leak between interrupts is the root cause

Last touched: 2026-05-12. All commits up to `4814251` pushed to `origin/main`.

## Real root cause (FIFTH revision, this is the right one)

Host harness now reproduces the bug via `tests/host/test_charin_dispatch.c`, `test_charin_timeline.c`, `test_charin_trace.c`, and the decisive `test_novac_dca.c`. Fast iteration loop, no flash cycle needed.

**The bug**: yaAGC's engine state leaks `FBANK` between interrupt contexts. Two identical KEYRUPT1 → NOVAC dispatches on host produce **different** FBANK values at the moment of NOVAC's `DCA 0`:

| Cycle | Q (caller's return) | FBANK | A (post-DCA) | L | Notes |
|---|---|---|---|---|---|
| 3457 | 02037 | **00201** | 00000 | 00000 | First DCA call ever — reads zeros |
| 122935 | 03311 (KEYRUPT1 -> CHARIN) | **11002** (bank 4) | 00013 | 00001 | First auto-RSET CHARIN |
| 200056 | 03311 (KEYRUPT1 -> CHARIN, again) | **16516** (bank 7!) | 00043 | 00001 | After manual VERB keypress |

Same code path (KEYRUPT1 lead-in → handler → ACCEPTUP → TC NOVAC, with Q=03311 = the address of the inline 2CADR words after `TC NOVAC`), but different `FBANK` at the DCA moment. The 2CADR words live at bank `BBANK_at_TC_NOVAC` offset `(03311 - 02000) = 01311`, so different banks read different words.

`FBANK` should be stable for any specific KEYRUPT1 invocation — the lead-in sets it via `CAF KEYRPTBB; XCH BBANK`. But by the time DCA runs, FBANK has drifted.

The drift is caused by **incomplete bank restoration on RESUME** in yaAGC's engine. Looking at `third_party/virtualagc/yaAGC/agc_engine.c:2874-2882`, the RESUME instruction does:
```c
State->NextZ = c (RegZRUPT) - 1;   // restore Z from ZRUPT
State->InIsr = 0;                   // exit ISR
State->SubstituteInstruction = 1;   // exec BRUPT next
```

It does **not** restore BBANK/FBANK from BANKRUPT. AGC convention says each ISR handler must do `LXCH BANKRUPT; TC RESUME` (or equivalent) before RESUME to restore the caller's bank. If some ISR handler in Luminary forgets, FBANK leaks to the next context.

Or: our integration's `WriteIO` / `ChannelOutput` callback might be mutating FBANK as a side-effect of channel writes. Worth auditing.

## What works (don't break)
- Boot to clean DSKY (host-side ERROR catches the boot NW transient).
- COMP ACTY blinks (Luminary's executive *is* executing instructions during the DOWNRUPT gaps).
- `peripheral_stub_tick` restoration of `ch030`/`ch033` baselines and IMODES30/IMODES33 fresh-start values. The slot-0 ghost-clear hack was tried (commit history) and reverted; it didn't help.
- Host test suite: 16/16 passing (including the new diagnostics, which PASS by printing rather than asserting).
- `tests/host/test_replay_apollo11_launch.c` (decoder validated against recorded yaDSKY2 transcript).
- Ring-buffer race in `channel_router_post_key` / `channel_router_pump_input` is now closed (`portMUX_TYPE` critical section).
- `CONFIG_AGC_TRACE_KEYRUPT1` dispatcher + follow-ISR trace infrastructure.

## Concrete next-session work

1. **Audit yaAGC's RESUME path.** Confirm whether RESUME is supposed to restore FBANK from BANKRUPT (which would mean our engine is missing that restore — a patch to `agc_engine.c` under `#ifdef ESP_AGC_FIX_FBANK_RESUME`), OR whether each ISR handler is expected to do it explicitly (which would mean Luminary itself has a missing restore somewhere — much harder, indicates an upstream yaAGC bug).

2. **Cross-reference Carl Wittnebert's Pi build and MKme/AGC_DSKY_Replica.** Both run real yaAGC + Luminary on hardware and reportedly work. They might have a small patch or integration tweak that addresses this. Check `git log` / issues in those repos for FBANK/BANKRUPT-related changes.

3. **Find which ISR is leaking FBANK.** Add a tracer that logs `FBANK` at every `InIsr` 1→0 transition (RESUME exit). Compare what each ISR leaves FBANK at versus what BANKRUPT held on entry. The leaker will be the ISR where they don't match.

4. **Once the leak is patched and CHARIN dispatches**, host `test_charin_dispatch.c` should see `DSPLOCK` transition to 1 (current behavior: stays at 0). On hardware, V35E via curl should light all DSKY segments.

5. **Real assertion-bearing host tests** for V35E and V37E00E (P1.5 from the original plan). The current `test_lamp_test.c` and `test_p00_select.c` print-only versions need conversion to assert specific dsky_state digits and lamps.

## Tried this session and unhelpful
- PRIORITY slots init to -0 (commit `c95a451`). Doesn't break anything, kept in tree. AGC convention so defensible.
- Slot-0 ghost clear in `peripheral_stub_tick`. Reverted — didn't help (the slot 4 ghost has same problem, doesn't fit our condition).
- Neutering `peripheral_stub_tick` entirely. Same bug appears.

## Files in tree after this session
- `tests/host/test_charin_dispatch.c` — the failing-as-expected test (in run list)
- `tests/host/test_charin_timeline.c` — standalone single-step timeline
- `tests/host/test_charin_trace.c` — slot-lifecycle tracker
- `tests/host/test_novac_dca.c` — the FBANK-leak smoking gun

## What is the actual problem (FOURTH revision)

**Four framings have been tried so far:**

1. *"KEYRUPT1 fires, NOVAC schedules CHARIN with bad CADR `077615`."* — Right symptom (077615 is real in some cell), wrong cause.
2. *"KEYRUPT1 never fires on hardware."* — Monitor rate-suppression artifact. Wrong.
3. *"KEYRUPT1 dispatches but the handler body or NOVAC has a bug."* — Wrong; follow-ISR trace showed the handler runs correctly.
4. *"Slot 0 has PRIO=030110 CADR[0]=0 boot ghost that the executive dispatches to address 0."* — **Wrong cell read.** `Erasable[0][0170]` (which I called "CADR[0]") is actually slot 1's MPAC scratchpad, not a CADR. Per `ERASABLE_ASSIGNMENTS.agc:388-395`, the 12-word slot layout is MPAC(7) / MODE / LOC / BANKSET / PUSHLOC / PRIORITY — with PRIORITY as the LAST cell (offset 11) and the job's entry address in LOC (offset 8). So slot 0's actual CADR is in `Erasable[0][0163]`, not `0170`. The 077615 value is just a piece of MPAC junk from slot 1.

**Current corrected reading** (from `commit c95a451` trace, which dumps the full slot cells):

```
slot0: MODE=03534 LOC=77776 BANKSET=20020 PUSHLOC=10006 PRIORITY=30110
slot1: MODE=00000 LOC=00000 BANKSET=03250 PUSHLOC=10006 PRIORITY=00110
```

Both slots **unchanged** across the entire KEYRUPT1 ISR. So NOVAC2 wrote to slot >= 2 (the trace doesn't currently dump those). Slot 0's `LOC=77776` (= -1 in 1's-complement AGC) is unusual; might be a "completed" or stale value. Slot 1's `LOC=0, PRIO=00110` (= FAKEPRET alone, with no CHRPRIO contribution) is also odd.

**The real symptom** observed via `curl -X POST --data V/3/5/E http://192.168.1.23/key` on hardware: no ch010 row writes follow the keypress sequence. CHARIN is not running. But all four framings above have been variously wrong about *why*.

**Current framing — investigate which slot NOVAC2 wrote to and whether CHARIN dispatches:**

After capturing a full KEYRUPT1 ISR via the new follow-ISR tracer (commit `f14d954`), the entire chain works:
| Step | Z | Operation | Observed |
|---|---|---|---|
| Lead-in | 04024-04027 | DXCH ARUPT / CAF KEYRPTBB / XCH BBANK / TCF KEYRUPT1 | ✓ |
| Handler body (bank 4) | 03274-03277 | TS BANKRUPT / XCH Q / TS QRUPT / TC LODSAMPT | ✓ |
| LODSAMPT (bank 2 fixed-fixed) | 04400-04403 | time-snatch | ✓ |
| Handler continues | 03300-03307 | CAF LOW5 / EXTEND / **RAND MNKEYIN** / TS RUPTREG4 / flag manipulation | `A=00022` ✓ keycode reaches A |
| ACCEPTUP | 03310 | TC NOVAC, sets Q=03311 | ✓ |
| NOVAC | 05072-05077 | INHINT / AD FAKEPRET / TS NEWPRIO / EXTEND / INDEX Q | ✓ |
| **DCA 0** | 05100 | reads two words at Q=03311 in bank 4 → `A=02077, L=60101` | ✓ these *are* the real 2CADR CHARIN words at the actual location (bank 4 offset 01311, NOT the fixed-fixed 04041/04042 that the now-invalid `test_cadr_resolution.c` assumed) |
| NOVAC2 + FINDSLOT | 02625-02670 (bank 2) | slot allocation | ✓ a slot gets allocated |
| RESUME | 05270-05276 | exits ISR | ✓ |

**The actual situation needs more data.** The corrected trace shows slot 0 and slot 1 are *not* where NOVAC2 puts the new CHARIN job — both slots are unchanged across the ISR. Need to extend the trace to dump slots 2-7 to find where it actually landed, and to verify whether the AGC executive's job-search ever picks that slot to dispatch CHARIN.

What we DO know after this session:
- The keypress chain works: post → pump → InterruptRequests[5] → KEYRUPT1 dispatch → handler → RAND MNKEYIN gets the correct keycode → ACCEPTUP → TC NOVAC.
- NOVAC's DCA reads the 2CADR CHARIN words correctly (`A=02077, L=60101` at bank 4 offset 01311).
- NOVAC2 runs slot allocation in bank 2 banked fixed (Z=02625-02670).
- ISR RESUMEs cleanly.
- After all that, slot 0 (PRIO=30110 LOC=77776) and slot 1 (PRIO=00110 LOC=0) are both unchanged. The new CHARIN job landed somewhere we're not currently watching.
- V35E keypress sequence produces no ch010 row writes — CHARIN isn't actually executing visible code.

What's STILL unknown:
- Which slot does NOVAC2 actually write to?
- Does the executive ever pick that slot?
- If it does, does CHARIN's body actually execute its first instruction (`XCH DSPLOCK`)?
- If so, why are there no resulting ch010 writes?

These are the questions for the next session.

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

## Tried and ruled out (or unhelpful)

1. **PRIORITY slot init to 077777 (-0).** Tried in commit `c95a451`. The `agc_init.c` change initializes all 8 PRIORITY cells to -0 (the AGC convention for "free slot") instead of leaving them zero-cleared. The fix is defensible regardless and is kept in tree, but it does not make V35E work on hardware. Slot 0 still ends up with PRIO=30110 after FRESH-START runs, so Luminary writes the priority regardless of our init.

2. **Ring-buffer portMUX (commit `375db8d`).** Was a real concurrency defect (the producer/consumer ring had no synchronization). Now fixed but not the bug behind keypress-deafness.

3. **Banked-fixed handler trace (commit `f14d954`).** Surfaced the full KEYRUPT1 ISR; verified handler runs correctly through to RESUME. Trace coverage is good for the ISR itself.

## Hypotheses for next session

1. **Find which slot NOVAC2 writes to.** Extend the keyrupt_trace_step ENTRY/RESUME dumps to cover all 8 slots, not just slot 0 and 1. Compare ENTRY vs RESUME to spot the slot that got written. Then watch that slot's LOC for dispatch.

2. **Trace the executive's main loop.** The job-search code runs continuously between ISRs. Probably lives at a known fixed address. Once we know which slot has CHARIN, watch whether the executive ever loads its LOC into RegZ.

3. **Watch DSPLOCK directly between ISRs.** Currently the trace's DSPLOCK delta-watch only fires while inside an ISR (because the follow-ISR latch gates it). Add a separate global DSPLOCK watch in `dispatch_trace_step` so we see if/when CHARIN's `XCH DSPLOCK` ever fires — answering "does CHARIN's body ever execute?"

4. **Confirm via host harness.** Add a host test that walks the same path: boot Luminary, post RSET, step 200k cycles, check `Erasable[2][012]` (DSPLOCK). If DSPLOCK transitions 0→1, host harness sees CHARIN run; if not, host reproduces hardware behavior (and we have a faster reproduction loop than the flash cycle).

Hypothesis 4 is the cheapest and most useful: a host test that asserts DSPLOCK is set after a keypress. If host PASSES (DSPLOCK becomes 1), hardware-specific issue (race, timing); if host FAILS too, fundamental engine/init bug we can iterate on without flashing.

## Concrete next-session work

1. **Try hypothesis 3 first — the 077777 init fix.** In `components/agc_core/agc_init.c::init_cpu_state`, after the erasable zero-clear, add:
   ```c
   // Mark all 8 executive PRIORITY slots as free (AGC convention: -0 = 077777).
   // Without this, slots are 0 = "occupied at priority 0" = dispatchable
   // (priority 0 is the lowest, but the executive still finds them).
   // Luminary's FRESH-START scheduler iterates looking for free slots
   // and may not handle the zeroed state we leave.
   for (int slot = 0; slot < 8; slot++) {
       State->Erasable[0][0167 + slot * 014] = 077777;
   }
   ```
   Build, flash, observe whether the boot-time ghost (PRIO[0]=030110 CADR[0]=0) still appears in the trace. If it goes away and CHARIN starts dispatching after a keypress — done.

2. **If hypothesis 3 fails — trace every boot-time NOVAC call.** Re-enable `CONFIG_AGC_TRACE_KEYRUPT1=y` AND extend the trace to fire on `Z == 05072` (NOVAC entry address, observed this session) regardless of `InIsr`. Capture from cycle 0 onwards. The first NOVAC call's A/L post-DCA will tell us whether the bug is engine 2CADR fetch (hypothesis 2) or ROM-load (hypothesis 1).

3. **Add a real assertion-bearing host test** for the slot-0 ghost: boot the engine, step ~200k cycles, assert that `Erasable[0][0167] == 077777` OR a valid CADR is also present (`Erasable[0][0170] != 0`). Currently `test_executive_state.c` just prints; turn it into a pass/fail. This becomes the regression guard for the fix.

4. **Once the ghost is gone, the V35E lamp test should "just work."** Drive `V`, `3`, `5`, `E` via curl POST → all DSKY segments light up. That unblocks Phase 1 verification. Replace the assertion-free `tests/host/test_lamp_test.c` and `test_p00_select.c` with real-assertion versions (P1.5 from this session's plan, already a task but not started).

5. **Note: `test_cadr_resolution.c` is now obsolete.** It was checking `Fixed[2][041..042]` assuming TC NOVAC was at fixed-fixed 04040. The actual TC NOVAC in Luminary099 is at 03310 (banked bank 4) and the 2CADR is at bank 4 offset 01311. Either delete the test or rewrite it to check the actual location.

6. **Spurious-keypress note**: a second keypress (`code=07`) appeared in trace this session without anything in code posting it. Touch task noise. Debounce/threshold in `components/dsky_input/touch.c` would fix it. Not urgent.

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
