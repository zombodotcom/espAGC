# PROG ALARM watchdog: auto-RSET + peripheral_stub

Status: design accepted 2026-05-10. Scope: options (a) + (b) from `docs/SESSION_NOTES.md`. Option (c) — full peripheral simulation for P63 descent — explicitly deferred.

## Problem

Luminary099 latches PROG ALARM at boot because the IMU monitoring loop in `T4RUPT_PROGRAM.agc::T4JOB` sees missing peripherals (no CDU counters ticking, no radar replies). The user RSET-clears the lamp, but Luminary's ERROR routine re-asserts it on the next mode-switch cycle because IMODES30/IMODES33 fault flags get re-set from the channel reads. Net visible effect: `RST` + `PROG` warning lamps steady yellow, registers R1/R2/R3 blank.

## Goal

Keep PROG ALARM clear at idle (no V37 program selected) so DSKY registers display normally. Stay out of (c)'s scope: do *not* fake CDU integration — when a real P-program runs, alarms are expected.

Non-goals:
- Running V37E63E to landing (needs (c)).
- Fixing PRO key path (separate task in session notes).
- Anything visible on the LCD beyond clearing the alarm.

## Architecture

```
                                  ┌─────────────────────────────┐
                                  │ channel_router_on_routine() │   ← engine calls this
                                  │ (every ~100 ms wall-time)   │     each ChannelRoutine
                                  └──────────────┬──────────────┘     tick
                                                 │
                          ┌──────────────────────┼──────────────────────┐
                          ▼                      ▼                      ▼
            (existing diag log,            peripheral_stub_tick     auto-RSET one-shot
             every 256 ticks)              (every tick)             (fires once at ~5 s)
                                                │                          │
                                                ▼                          ▼
                                   agc_core_state() → state          channel_router_
                                   ├ InputChannel[030] = 0o36377     post_key(DSKY_KEY_RSET)
                                   ├ InputChannel[033] = 0o77777
                                   └ Erasable[…] IMODES30/33
                                     fault bits cleared
```

Both pieces hook into `channel_router_on_routine()`, which already runs at the right cadence. No new FreeRTOS task.

## Component 1 — auto-RSET (lives inline in `channel_router.c`)

About 6 lines of code, gated by Kconfig `CONFIG_AGC_AUTO_RSET_AT_BOOT` (default `y`).

```c
#if CONFIG_AGC_AUTO_RSET_AT_BOOT
    static bool g_did_boot_rset = false;
    if (!g_did_boot_rset && g_routine_count >= 50) {  // ~5 s wall-time
        channel_router_post_key(DSKY_KEY_RSET);
        g_did_boot_rset = true;
        ESP_LOGI(TAG, "auto-RSET posted at boot");
    }
#endif
```

Why 50 ticks: ChannelRoutine fires every ~02000 engine cycles. With the engine running ~819 kHz nominal, that's ~100 ms wall-time per tick, so 50 ≈ 5 s. Generous margin for WiFi/NVS settle. Cosmetic side effect: PROG ALARM lamp visible for ~5 s before flush.

One-shot — the latch never resets. If the alarm comes back later, peripheral_stub is responsible for keeping it clear; auto-RSET is only the initial flush.

## Component 2 — `peripheral_stub` (new component)

### Files

```
components/peripheral_stub/
├── CMakeLists.txt
├── include/peripheral_stub.h
└── peripheral_stub.c
```

### Public API

```c
// peripheral_stub.h
#pragma once
#include "yaAGC.h"
#include "agc_engine.h"

void peripheral_stub_init(void);
void peripheral_stub_tick(agc_t *state);
```

### Behavior per tick

Called from `channel_router_on_routine()` before the existing diag/log block.

1. **Restore peripheral channel baselines.** Idempotent full re-assignment:
   - `state->InputChannel[030] = 0o36377` — healthy LM: IMU operating, LGC in control, temp OK, all other signals not-present (no fault).
   - `state->InputChannel[033] = 0o77777` — no AGC warning, no PIPA fail, no oscillator fail.
   - Leave `031` and `032` alone. `032` carries the PRO key bit, `031` carries RHC/THC stick state — both are touched by the input transport path, and the agc_init.c initial values are already correct.

2. **Clear IMODES30 and IMODES33 fault bits in erasable memory.** Addresses to be confirmed from `third_party/virtualagc/Luminary099/ERASABLE_ASSIGNMENTS.agc` during implementation. Expected form:

   ```c
   // IMODES30 mirrors ch030 with sticky fault bits
   state->Erasable[IMODES30_BANK][IMODES30_OFFSET] &= ~IMODES30_FAULT_MASK;
   // IMODES33 mirrors ch033 with sticky fault bits
   state->Erasable[IMODES33_BANK][IMODES33_OFFSET] &= ~IMODES33_FAULT_MASK;
   ```

   If addresses turn out to be undocumented or behind macro indirection, fall back to channel-only restoration and accept that PROG ALARM may flash briefly between ticks before stabilizing.

### Integration

One call added to `channel_router_on_routine()` at the top:

```c
void channel_router_on_routine(void)
{
    peripheral_stub_tick(agc_core_state());

    /* ...existing auto-RSET one-shot... */
    /* ...existing diag log every 256 ticks... */
}
```

## Kconfig

Add to `components/channel_router/Kconfig.projbuild` (or wherever channel_router's Kconfig lives):

```
config AGC_AUTO_RSET_AT_BOOT
    bool "Send a synthetic RSET ~5 s after boot to flush PROG ALARM"
    default y
    help
      Posts one RSET keycode to channel 015 after Luminary settles. Used
      together with the peripheral_stub watchdog to keep the alarm lamp
      clear at idle. Disable to observe raw alarm behavior for debugging.
```

`peripheral_stub` itself is unconditional — if it's compiled in, it ticks. Adding a second Kconfig flag for it is overkill; not having it defeats the whole component.

## Testing

Two new host tests, both run under `tests/host/Makefile`. Test count: 11 → 13.

### `test_auto_rset_at_boot.c`

- `agc_core_init(rom, size)` with Luminary099.
- Step the engine until `g_routine_count >= 50` (using existing `agc_harness` helpers to drive ticks).
- Assert: a RSET keycode (18) is in `channel_router`'s key ring, AND after one more engine pass to consume it, `dsky_state.prog_alarm` is `false`.

### `test_peripheral_stub_clears_imodes.c`

- Initialize state.
- Set fault bits in `state->Erasable[…]` for IMODES30 and IMODES33.
- Call `peripheral_stub_tick(state)`.
- Assert: those bits are cleared. Also assert `state->InputChannel[030] == 0o36377` and `state->InputChannel[033] == 0o77777` after a tick that had stomped values.

## Trade-offs and known limits

- **IMODES address lookup risk.** If `ERASABLE_ASSIGNMENTS.agc` doesn't pin them or they move per ROM version, the erasable-poke path becomes brittle. Mitigation: fall back to channel-only restoration; document the address in a comment with line reference.
- **No CDU counter feed.** Running V37E63E (P63 descent) will re-alarm within seconds — T4JOB needs fresh CDU counts. That's option (c)'s job, not this spec.
- **5-second visible flicker.** Until auto-RSET fires, PROG ALARM is visible on screen. Acceptable since it's only at boot, but if it bothers users we can shorten the delay later — only constraint is Luminary needs to be past startup before RSET means anything.
- **Auto-RSET vs (b) overlap.** (b) alone would eventually clear the alarm too (once IMODES bits get cleared), but Luminary's ALARM routine latches the lamp via DSPTAB +11D bit 9, which only `WriteIO(ch015, RSET)` clears via the engine's hardware-direct flip-flop. So (a) and (b) really are complementary: (a) flushes the latch once, (b) keeps the underlying fault state from re-arming it.

## Implementation order

1. Look up IMODES30/IMODES33 erasable addresses in `Luminary099/ERASABLE_ASSIGNMENTS.agc`.
2. Scaffold `components/peripheral_stub/` with empty tick function.
3. Add host test #1 (auto-RSET) — implement Kconfig + the one-shot code to make it pass.
4. Add host test #2 (peripheral_stub) — implement the tick body to make it pass.
5. Wire `peripheral_stub_tick()` call into `channel_router_on_routine()`.
6. Run full host suite (expect 13/13 PASS).
7. Flash to hardware, confirm: PROG/RST lamps yellow at boot, clear within ~5 s, stay clear at idle.

## Out of scope (explicit)

- CDU counter simulation (option c).
- Radar return simulation (option c).
- PRO key path wiring (`DSKY_KEY_PRO=63` sentinel → ch032 bit 0x4000). Separate task in session notes.
- Any change to the renderer, layout, or LVGL.
- Apollo 11 landing replay enabling — depends on (c).
