# Next session handoff — boot-time slot-0 ghost is the actual bug

Last touched: 2026-05-11 (very late). All commits up to `f14d954` pushed to `origin/main`.

## What is the actual problem (THIRD revision, 2026-05-11 very late)

**Three framings behind us:**

1. *"KEYRUPT1 fires, NOVAC schedules CHARIN with bad CADR `077615`, CHARIN never runs."* — Right symptom (077615 is real), wrong cause.
2. *"KEYRUPT1 never fires on hardware."* — Monitor rate-suppression artifact. Wrong.
3. *"KEYRUPT1 dispatches but the handler body or NOVAC has a bug."* — Wrong; the follow-ISR trace this session showed the entire handler runs correctly.

**Current framing — the bug is at boot, before any keypress:**

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

**The actual problem:** `PRIORITY[0]=030110 CADR[0]=00000` was already set **before the keypress arrived**, and was **unchanged** by the entire KEYRUPT1 ISR. NOVAC2 didn't write to slot 0 because the slot was already "occupied" (PRIO is positive, not the `077777=-0` "free" sentinel). NOVAC2 must have used a different slot.

The 077615 corruption seen in `test_executive_state.c` and at the start of subsequent KEYRUPT1 ISRs comes from between RESUME of one ISR and the next ChannelRoutine: the executive job-search picks slot 0 (highest priority `030110`), tries to dispatch to CADR=0, runs garbage from address 0 (= RegA), eventually corrupts CADR[0] to 077615.

So the chain is:
1. **Boot** → Luminary's FRESH-START → first NOVAC call somewhere → slot 0 gets `PRIO=030110 CADR=00000` (the CADR write didn't take, even though the PRIO write did).
2. **Executive runs** → finds slot 0 highest priority → dispatches to CADR=0 → runs garbage → writes 077615 to some cell that happens to be CADR[0].
3. **Keypress arrives** → KEYRUPT1 → NOVAC2 → schedules CHARIN to slot 1 or higher (slot 0 marked occupied).
4. **Executive runs again** → STILL picks slot 0 (highest priority, beats slot N's CHARIN at same priority by index tiebreaker) → garbage continues → CHARIN never gets to run.

That's why no keypress has visible effect: CHARIN is *scheduled* in a non-0 slot but never *dispatched* because slot 0's ghost permanently outranks it.

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

## Hypotheses for the boot-time slot-0 ghost

The ghost has `PRIO=030110 CADR=00000`. PRIO=030110 = CHRPRIO(030000) + FAKEPRET(00110). That's the priority NOVAC produces for a CHARIN-scheduled job. So at boot, **something called NOVAC with `A=030000` (CHRPRIO), but the 2CADR words at the indexed Q location were zero**.

1. **An early NOVAC call hits zero-padded 2CADR words.** The most likely scenario. During FRESH-START (`FRESH_START_AND_RESTART.agc`), Luminary calls NOVAC to schedule a startup job. That `TC NOVAC` is followed by a `2CADR` directive. yaYUL expands the 2CADR to two specific words. If our ROM-loader misplaces those words (off-by-one, wrong bank, wrong byte order at that specific page), the engine's `INDEX Q; DCA 0` reads zeros. Verify: trace earliest NOVAC call during boot (before any keypress); compare A/L after the DCA against expected encoded values.

2. **The 2CADR address calculation in our engine has a banking error specific to one of the FRESH-START callers.** The KEYRUPT1 case works (its 2CADR reads 02077/60101 correctly). Some other NOVAC call site might be in a bank where our engine resolves `INDEX Q` wrong. Verify: trace EVERY NOVAC call from boot, log A/L post-DCA.

3. **Luminary's FRESH-START specifically expects PRIORITY slots pre-set to `077777` (-0 = free) before it runs.** Our `agc_init.c` zeroes all erasable. Zero means "occupied at priority 0" — a real (lowest-priority) job. If Luminary's first NOVAC call iterates slots looking for free (=-0), it skips slot 0 because slot 0 isn't free; falls through; eventually overwrites slot 0 partially. Verify: try a one-line fix in `agc_init.c` that initializes `Erasable[0][0167 + slot*014] = 077777` for slots 0-7 before letting the engine run. If the ghost disappears and CHARIN starts dispatching, this is it.

4. **Our `peripheral_stub_tick` writes to a cell that overlaps PRIORITY[0]+CADR[0].** The stub does `Erasable[0][0375..0377] = 0` (FAILREG zeroing) and `Erasable[2][036] &= ~mask` (DSPTAB+11D). Neither overlaps with 0167/0170. But verify by binary-disabling `peripheral_stub_tick` temporarily and observing whether the slot-0 ghost still appears.

Hypothesis 3 is the cheapest to test (1 line) and matches the symptom most directly. Try first.

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
