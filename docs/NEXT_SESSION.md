# Next session — CHARIN scheduled but slot 0's job never ENDOFJOBs

Last touched: 2026-05-11. Test suite green (`mingw32-make run`: ALL PASS).

## Real root causes identified + fixed this iteration

1. **MASS = 0 at boot was the original blocker for 1/ACCS**. With LEMMASS=0, 1/ACCS's HIASCENT/LOASCENT bound checks all fall through to MASSFIX, which clamps LEMMASS to LOASCENT and `TCF F(MASS)`. F(MASS) doesn't re-check bounds — it runs STCTR loop. STCTR completes 3 iterations, COMMEQS computes EPSILON, GOODEPS1 computes COEFFR/COEFFQ, JACCUV computes 1JACCU/1JACCV, then 1/ACCONT (or 1/ACCRET). 1/ACCRET sets ACCSOKAY in DAPBOOLS via `ADS DAPBOOLS`.

   **Fix**: `peripheral_stub_init` writes MASS = FULLAPS (05050) to Erasable[2][244]. Equivalent to a V21N47E PAD LOAD.

2. **Software RESTART (POODOO/BAILOUT/ABORT2 → ENEMA → FRESH_START) clears DAPBOOLS to BOOLSTRT (21312)** — losing the ACCSOKAY bit. Without ACCSOKAY, DAPIDLER goes back to MOREIDLE forever.

   **Fix**: `peripheral_stub_tick` re-asserts MASS and `DAPBOOLS |= ACCSOKAY` every 200ms. Verified by `test_accsokay_wait.exe` and `test_redoctr.exe`.

3. **TC-Trap GOJAMs** were eliminated by Apollo11-launch.canned channel values + `InhibitAlarms=1`.

4. **Full LM_Simulator attitude model** added — 3-axis stable-member angles in milli-degrees, integrated from jet impulses observed on ch005/ch006, PCDU/MCDU pulses pushed when angle exceeds one-pulse threshold (≈11 mdeg).

## What still doesn't work

V35E does NOT produce VRB=[3,5]. After 100M cycles:
- DAPBOOLS=00004 (ACCSOKAY set persistently — confirmed)
- MASS=05050 (persistent — confirmed)
- Slot 1 has CHARIN signature (PRIO=30110 LOC=02077 BANK=60101 MPAC[0]=keycode)
- BUT `test_charin_real_trace.exe`: Z=02077 NEVER appears with `adjFB=040` (CHARIN's bank) — only with `adjFB=06` (T4RUPT). The executive never actually picks slot 1 for CPU.

The remaining gate: **slot 0 holds a job at PRIO=27110 that never reaches `TC ENDOFJOB`**. The Block II AGC executive only switches jobs at job-end (ADVAN's CCS NEWJOB) so as long as slot 0's job runs forever, slot 1's CHARIN waits forever.

`test_slot0_evolve.exe` shows slot 0 PRIO transitions 77777 → 27110 once at c=3483 and STAYS at 27110 for 100M+ cycles. LOC progresses through bank-7 addresses around MKRELEAS (`07,2056 TC IBNKCALL; 07,2057 CADR GOODEND`) — that's part of AOTMARK.agc. The job is making progress but never finishing.

Hypothesis: slot 0 is a background job (servicer / longcall) that's working but in an infinite-loop pattern. On real Apollo + Pi/Linux yaAGC + LM_Simulator, the same scenario presumably works because either (a) slot 0's job IS short and DOES ENDOFJOB regularly, or (b) Pi/Linux has a different mechanism we're missing.

Next investigation:
- Find what schedules slot 0's PRIO27 job at boot. Track its full lifecycle. Why does it never reach `TC ENDOFJOB`?
- Or alternatively: have peripheral_stub force ENDOFJOB on slot 0 periodically (this would be a corner cut — not preferred).
- Or: install yaAGC + LM_Simulator + yaDSKY2 on a Linux VM and observe the actual reference behavior.

## Root cause identified this session

The reason V35E doesn't light segments is **NOT** that CHARIN doesn't get scheduled. It IS scheduled (slot 2 gets `PRIO=30110 LOC=02077 BANKSET=60101`, the CHARIN signature) — but the engine takes a **GOJAM (software reset)** before the slot ever runs, wiping all allocations.

GOJAM source: **TC Trap alarm**. With continuous CDU pulses (`peripheral_stub_step`), yaAGC sees 150 TC-Traps per 1M cycles. Each GOJAM clears interrupt requests, channels 5-14, and resets the executive — so the CHARIN slot allocation is gone before slot 2 ever gets CPU time.

How to verify on a fresh run:

```sh
cd tests/host && mingw32-make test_check_restart.exe diag
ROM=../../build/roms/Luminary099.bin ./test_check_restart.exe 2>&1 | grep "Alarm:"
# Currently prints ~6 TCTrap + 2 NightWatchman per 1M cycles (with periodic
# channel-write tick only, no CDU pulses). With CDU pulses added, jumps to
# ~150 TCTrap + 4 NightWatchman.
```

The slot-2 timeline proof:

```sh
ROM=../../build/roms/Luminary099.bin ./test_slot2_post_verb.exe
# Output:
#  c=    0 slot2 PRIO=77777 LOC=02447     <-- leftover from prior job
#  c=  109 slot2 PRIO=30110 LOC=02447     <-- CHARIN being allocated
#  c= 1195 slot2 PRIO=77777 LOC=02077     <-- wiped by GOJAM (CHARIN bank
#                                           never actually ran — confirmed
#                                           by test_charin_real_trace
#                                           showing 0 hits in bank 40)
```

And `test_charin_real_trace.exe` shows **0 hits at Z=02077 with adjFB=040** (CHARIN's real address). Z=02077 hits 116 times in 200k cycles but ONLY with adjFB=06 (T4RUPT's `RXOR CHAN32`).

## What works (don't break)

- yaAGC engine and Luminary099 ROM load. Test suite green.
- KEYRUPT1 dispatches when a key is posted (IR5 fires, ch015 updates).
- NOVAC allocates a CHARIN slot correctly (slot 2 at PRIO=30110 LOC=02077 BANK=60101).
- **LM_Simulator wdata values now match exactly** (`peripheral_stub.c` lines 64-78): ch030=036331, ch031=077777, ch032=021777, ch033=057776. These are the bit-for-bit values from `Contributed/LM_Simulator/lm_simulator.tcl:570-572`.
- Diagnostic tests build under `mingw32-make diag`.

## What's blocked

- **Slot 0 stuck at PRIO=30110** (1/ACCS / GOODEPS1 in `AOSTASK_AND_AOSJOB.agc:216`) without CDU input. Won't terminate. Without 1/ACCS finishing, CHARIN at the same priority loses the slot-index tie-break forever.
- **CDU pulses cause TC-Trap GOJAMs** that wipe everything. Continuous pulses trigger them most often, but even a small burst doesn't fully resolve 1/ACCS.
- **DSKY digit rows stay payload=0**. Luminary writes the digit rows but DSPTAB+0..+10D never get loaded with real data because CHARIN never dispatches the verb-entry handlers.

## Increment C plan (revised)

The plan to "add IMU/DAP feedback so slot 0 frees" is correct *in principle* but needs to be done WITHOUT tripping TC Trap. Pi/Linux yaAGC running with LM_Simulator does not seem to suffer continuous GOJAMs, so something about our timing / channel state / CDU rate differs.

Concrete next steps:

1. **Verify Pi/Linux alarm rate**. Build yaAGC on Linux (or use a reference run), connect LM_Simulator and yaDSKY2 in their normal way, watch for "Alarm: TCTrap" / "Alarm: NightWatchman" prints. If Pi/Linux also generates these but ignores them, our problem is just slot-allocation timing (different fix). If Pi/Linux does NOT generate them, our peripheral simulation is doing something Pi/Linux does not.

2. **Diff our boot trace vs canonical**. The Apollo11-launch.canned file in `third_party/virtualagc/yaDSKY2/` is a recorded LM_Simulator/yaDSKY2 session. Replay it through our channel_router and compare the first 1M cycles of channel writes between our peripheral_stub and the canned trace.

3. **Find what makes 1/ACCS terminate without continuous pulses**. The hot Z address is `GOODEPS1` in bank 020 (= bank 16 source). The computation involves `1JACCQ`, `EPSILON`, `MP 0.35356`, `AD .7071`. Either MASS is wrong (causing DV by zero?) or APSFLBIT / FLGWRD10 state is wrong. Test `MASS` and `FLGWRD10` values during the stuck loop with a Z-conditional sampler.

4. **Get the DSKY working**. Once CHARIN can actually run, V35E should drive VBTSTLTS which loads DSPTAB+0..+10D with `OCT 05675` (the "ALL 8s" pattern) and lights the lamp row with `OCT 40674`.

## Diagnostic tests added this session

All under `mingw32-make diag`:

| Test | Purpose |
|---|---|
| `test_slot0_dwell` | Samples slot 0 / slot 4 PRIORITY every cycle. Reports distribution. |
| `test_dump_exec_state` | Full bank-0 cells 0150-0260 dump + per-slot decode at boot / +V / +3 / +5 / +E. |
| `test_dsptab_dump` | Reads DSPTAB cells (Erasable[2][023..036]) + OutputChannel10 per row. |
| `test_keypress_timeline` | 50k-cycle Z + IR5 + ch015 + slot 4 trace post-VERB. |
| `test_verb_capture` | VERBREG/NOUNREG/DSPCOUNT/DSPLOCK/CADRSTOR/MPAC0 after each key. |
| `test_find_verbreg` | Snapshot diff to find cells that change after each keypress. |
| `test_charin_state` | Same as verb_capture, focused on CHARIN-relevant cells. |
| `test_z_trace_after_verb` | Counts Z visits to CHARIN/VERB/CHARIN2 addresses. **Note: addresses overlap by bank**. |
| `test_charin_real_trace` | Bank-aware: counts Z=02077 hits only when `physBank=040`. |
| `test_charin_verify` | Prints (Z, FB, ch7, sb, physBank) for every Z=02077. |
| `test_slot2_post_verb` | Slot 2 PRIORITY transitions post-VERB — proves slot is allocated then wiped. |
| `test_check_restart` | Counts GOJAM hits, NightWatchman trips, ISR entries. **Critical**. |
| `test_cadrstor_when_charin` | What's CADRSTOR each time Z=02102 hits. |
| `test_stuck_z_trace` | Z histogram in a 3000-cycle window after 1M boot. |

## File map

| Path | Purpose |
|---|---|
| `components/peripheral_stub/peripheral_stub.c` | Channel feed (now using exact lm_simulator.tcl wdata values). One-shot CDU burst at init. Periodic tick currently only re-asserts ch030/ch033 baselines + FAILREG cleanup. |
| `components/agc_core/io_callbacks.c` | Engine I/O glue. `ChannelInput` calls `channel_router_pump_input`. |
| `components/channel_router/channel_router.c` | DSKY decoder + key ring. |
| `tests/host/agc_harness.c` | `harness_boot()` runs `channel_router_init` then `agc_core_init` then `peripheral_stub_init`. |
| `third_party/virtualagc/Contributed/LM_Simulator/lm_simulator.tcl:570-572` | Canonical Pi/Linux wdata values. |
| `third_party/virtualagc/Luminary099/AOSTASK_AND_AOSJOB.agc:107-220` | 1/ACCS routine. GOODEPS1 is at line 216. |
| `third_party/virtualagc/Luminary099/KEYRUPT,_UPRUPT.agc:39-59` | KEYRUPT1 → NOVAC CHARIN. |
| `third_party/virtualagc/Luminary099/PINBALL_GAME__BUTTONS_AND_LIGHTS.agc:475` | CHARIN entry. Dispatches to VERB/NUM/ENTER/etc. by keycode. |
| `third_party/virtualagc/Luminary099/PINBALL_GAME__BUTTONS_AND_LIGHTS.agc:3637` | VBTSTLTS (V35 handler — what we're trying to reach). |
| `/tmp/luminary.lst` | yaYUL listing. Rebuild via `cd third_party/virtualagc/yaYUL && ./yaYUL.exe ../Luminary099/MAIN.agc > /tmp/luminary.lst`. |

## What NOT to do (lessons from this session)

- **Don't assume Z values without bank context**. Z=02077 in bank 6 (T4RUPT) is *not* CHARIN. yaAGC's Z register is 12 bits; the physical address is `(FB + superbank << 10) | Z`. Bank 40 (CHARIN's bank) requires the superbank bit (`OutputChannel7 & 0100`) set.
- **Don't continuously inject CDU pulses**. Pi/Linux uses them but our build trips TC Trap with the same rate. Until we understand why our build differs, keep CDU rate low or one-shot.
- **Don't force IMODES30/IMODES33 to fresh values every tick**. Earlier `peripheral_stub_tick` did this and contributed to GOJAMs. Removed in this session.
- **Don't seed erasable cells to "unblock" Luminary**. Already tried (MASS, DAPBOOLS); Luminary's FRESH-START resets them. The simulator must drive state through the same WriteIO / UnprogrammedIncrement entry points LM_Simulator uses over its socket.
