# PROG ALARM watchdog Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate the latched boot-time PROG ALARM by adding a one-shot auto-RSET keypress and a periodic peripheral watchdog that resets channel inputs + IMODES erasable fault state.

**Architecture:** Both pieces hook into the existing `channel_router_on_routine()` callback (fires every `ChannelRoutineCount` tick, ~100 ms wall-time). Auto-RSET is ~6 lines inline in `channel_router.c` and gated by Kconfig. Peripheral watchdog is a new component `peripheral_stub` with a one-function public API (`peripheral_stub_tick(agc_t *)`), called from the same hook.

**Tech Stack:** ESP-IDF v6.0+ (target esp32), yaAGC engine (C), MinGW gcc for host tests.

**Spec:** `docs/superpowers/specs/2026-05-10-prog-alarm-watchdog-design.md`

---

## File Structure

- **Create:**
  - `components/channel_router/Kconfig.projbuild` — new file, adds `CONFIG_AGC_AUTO_RSET_AT_BOOT`.
  - `components/peripheral_stub/CMakeLists.txt` — registers the new component.
  - `components/peripheral_stub/include/peripheral_stub.h` — public API.
  - `components/peripheral_stub/peripheral_stub.c` — implementation.
  - `tests/host/test_auto_rset_at_boot.c` — Layer 2 host test for auto-RSET.
  - `tests/host/test_peripheral_stub_clears_imodes.c` — Layer 2 host test for the watchdog.

- **Modify:**
  - `components/channel_router/channel_router.c` — add auto-RSET one-shot + call `peripheral_stub_tick()` from `channel_router_on_routine()`.
  - `components/channel_router/CMakeLists.txt` — add `peripheral_stub` to `PRIV_REQUIRES`.
  - `tests/host/Makefile` — extend `HARNESS_SRCS` with `peripheral_stub.c`, add the two new tests to `LAYER2_TESTS`, add `peripheral_stub/include` to `HARNESS_INC`.

## Key constants used throughout

```c
// AGC erasable addresses for Luminary099 mode-switch mirrors.
// Source: build artifact MAIN.agc.html shows IMODES30 at virtual address
// octal 01302, IMODES33 at 01303. yaAGC's agc_t::Erasable[8][0400]
// indexes by (address / 0400, address % 0400):
//   01302 / 0400 = 2 rem 0302  -> Erasable[2][0302]
//   01303 / 0400 = 2 rem 0303  -> Erasable[2][0303]
//
// Fresh-start values (per T4RUPT_PROGRAM.agc comments at lines 273, 527):
//   IMODES30 = 037411 octal
//   IMODES33 = 016000 octal
#define IMODES30_BANK    2
#define IMODES30_OFFSET  0302
#define IMODES30_FRESH   037411
#define IMODES33_BANK    2
#define IMODES33_OFFSET  0303
#define IMODES33_FRESH   016000

// Channel-input baselines (already used in agc_init.c).
//   ch030 = 036377: healthy LM, IMU operating, LGC control, temp OK.
//   ch033 = 077777: no AGC warning, no PIPA fail, no oscillator fail.
#define CH030_BASELINE   036377
#define CH033_BASELINE   077777
```

---

## Task 1: Add Kconfig flag for auto-RSET

**Files:**
- Create: `components/channel_router/Kconfig.projbuild`

- [ ] **Step 1: Create the Kconfig file**

`components/channel_router/Kconfig.projbuild`:

```
menu "espAGC PROG ALARM watchdog"

config AGC_AUTO_RSET_AT_BOOT
    bool "Send a synthetic RSET ~5 s after boot to flush PROG ALARM"
    default y
    help
        Posts one DSKY_KEY_RSET keycode to channel 015 once the engine
        has run ~50 ChannelRoutine ticks (about 5 s wall-time). Used
        together with the peripheral_stub watchdog to keep the PROG
        ALARM lamp clear at idle. Disable to observe raw alarm
        behavior for debugging the underlying peripheral state.

endmenu
```

- [ ] **Step 2: Commit**

```
git add components/channel_router/Kconfig.projbuild
git commit -m "channel_router: add Kconfig for AGC_AUTO_RSET_AT_BOOT (default y)"
```

---

## Task 2: Write the failing host test for auto-RSET

**Files:**
- Create: `tests/host/test_auto_rset_at_boot.c`
- Modify: `tests/host/Makefile` (add to LAYER2_TESTS list)

- [ ] **Step 1: Create the test**

`tests/host/test_auto_rset_at_boot.c`:

```c
// test_auto_rset_at_boot — verifies that the channel_router posts a
// synthetic DSKY_KEY_RSET keypress after ~50 ChannelRoutine ticks at
// boot. This is option (a) from docs/superpowers/specs/2026-05-10-
// prog-alarm-watchdog-design.md: the initial flush that clears PROG
// ALARM's latch via the engine's hardware-direct RSET path.

#include "agc_harness.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"

#include <stdio.h>

extern agc_t *agc_core_state(void);

int main(void)
{
    harness_boot();

    // Step the engine long enough that channel_router_on_routine() has
    // been called >= 50 times. The engine calls ChannelRoutine every
    // 02000 cycles, so 50 * 02000 = 100000 cycles is the floor. Add a
    // ~5x margin and post-RSET consume room.
    harness_step(500000);

    // After auto-RSET fires, RSET keycode (18) should have been routed
    // through WriteIO(015, ...) — which clears RestartLight in the
    // engine. We assert restart lamp is clear as the observable side
    // effect.
    dsky_state_t s;
    harness_snapshot(&s);
    printf("post-boot: restart=%d prog_alarm=%d\n", s.restart, s.prog_alarm);

    ASSERT(!s.restart,
        "auto-RSET should have flushed RestartLight via WriteIO(ch015, RSET)");

    PASS();
}
```

- [ ] **Step 2: Add to Makefile**

Modify `tests/host/Makefile` line 55 (the `LAYER2_TESTS :=` definition). Replace:

```
LAYER2_TESTS := test_alarm_at_boot test_p00_select test_lamp_test \
                test_rset_clears_alarms test_replay_apollo11_launch
```

with:

```
LAYER2_TESTS := test_alarm_at_boot test_p00_select test_lamp_test \
                test_rset_clears_alarms test_replay_apollo11_launch \
                test_auto_rset_at_boot
```

- [ ] **Step 3: Run the test — expect FAIL**

```
cd tests/host
mingw32-make run-test_auto_rset_at_boot
```

Expected: FAIL with `ASSERT failed: auto-RSET should have flushed RestartLight...` — because auto-RSET isn't implemented yet. (RestartLight stays latched on a normal Luminary099 boot until the user presses RSET.)

- [ ] **Step 4: Commit (test only)**

```
git add tests/host/test_auto_rset_at_boot.c tests/host/Makefile
git commit -m "tests/host: failing test for auto-RSET at boot"
```

---

## Task 3: Implement auto-RSET in channel_router.c

**Files:**
- Modify: `components/channel_router/channel_router.c`

- [ ] **Step 1: Include dsky_keys.h if not already**

Check `components/channel_router/channel_router.c` line 16 area for `#include "dsky_keys.h"`. If absent, add after `#include "agc_engine.h"`:

```c
#include "dsky_keys.h"
```

- [ ] **Step 2: Add the one-shot logic into `channel_router_on_routine()`**

In `components/channel_router/channel_router.c`, find `void channel_router_on_routine(void)` (currently around line 261). Add this block as the **first thing inside the function**, before the existing `if (++g_routine_count % 256 != 0) return;` line:

```c
    // (a) Auto-RSET one-shot. After Luminary settles past GOJAM and
    // peripheral checks, post one synthetic RSET keypress so the
    // engine's hardware-direct RestartLight clear (agc_engine.c:586)
    // fires. Combined with peripheral_stub keeping IMODES* fault bits
    // clear, this keeps PROG ALARM extinguished at idle. See
    // docs/superpowers/specs/2026-05-10-prog-alarm-watchdog-design.md.
#if CONFIG_AGC_AUTO_RSET_AT_BOOT
    static bool s_did_boot_rset = false;
    if (!s_did_boot_rset && g_routine_count >= 50) {
        channel_router_post_key(DSKY_KEY_RSET);
        s_did_boot_rset = true;
        ESP_LOGI(TAG, "auto-RSET posted at boot (tick %d)", g_routine_count);
    }
#endif
```

Note: `g_routine_count` is already declared at file scope. The existing `if (++g_routine_count % 256 != 0) return;` line increments it, so the check must happen *before* that line (otherwise the auto-RSET test gets a count of 0 forever on the 255 ticks between dumps). The post-increment in `++g_routine_count` is already correct since we want to fire on the 50th tick or later.

Wait — the existing line increments and tests for 256. We need our check to also see the incremented value. The cleanest fix: increment first, then do both checks:

Replace the existing line:
```c
    if (++g_routine_count % 256 != 0) return;
```

with:
```c
    g_routine_count++;

#if CONFIG_AGC_AUTO_RSET_AT_BOOT
    static bool s_did_boot_rset = false;
    if (!s_did_boot_rset && g_routine_count >= 50) {
        channel_router_post_key(DSKY_KEY_RSET);
        s_did_boot_rset = true;
        ESP_LOGI(TAG, "auto-RSET posted at boot (tick %d)", g_routine_count);
    }
#endif

    if (g_routine_count % 256 != 0) return;
```

This preserves the existing 256-tick dump cadence and adds the one-shot in the right place.

- [ ] **Step 3: Make CONFIG_AGC_AUTO_RSET_AT_BOOT available to host tests**

The host build doesn't run Kconfig. Add a fallback at the top of `channel_router.c` near other includes:

```c
#ifndef CONFIG_AGC_AUTO_RSET_AT_BOOT
#define CONFIG_AGC_AUTO_RSET_AT_BOOT 1
#endif
```

Place this after `#include "esp_log.h"` and before the `static const char *TAG = ...` line.

- [ ] **Step 4: Run the test — expect PASS**

```
cd tests/host
mingw32-make run-test_auto_rset_at_boot
```

Expected: `--- test_auto_rset_at_boot ---` followed by `auto-RSET posted at boot (tick 50)` and `PASS`.

- [ ] **Step 5: Run the full Layer-2 suite to confirm no regressions**

```
cd tests/host
mingw32-make run
```

Expected: all 12 tests PASS (11 prior + 1 new). If `test_rset_clears_alarms` now fails — the auto-RSET is firing too early and stealing the manual RSET — that means the test's `for batch < 50` loop is reaching NW-clear inside the auto-RSET window. Re-run; if reproducible, increase the auto-RSET threshold to `g_routine_count >= 200` (the manual test posts its own RSET at ~5M cycles which is roughly tick ~2500, so 200 keeps a wide margin and doesn't change real-world behavior much).

- [ ] **Step 6: Commit**

```
git add components/channel_router/channel_router.c
git commit -m "channel_router: auto-RSET one-shot at boot tick 50 (option a)"
```

---

## Task 4: Scaffold the peripheral_stub component

**Files:**
- Create: `components/peripheral_stub/CMakeLists.txt`
- Create: `components/peripheral_stub/include/peripheral_stub.h`
- Create: `components/peripheral_stub/peripheral_stub.c`

- [ ] **Step 1: Create CMakeLists.txt**

`components/peripheral_stub/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS         "peripheral_stub.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../third_party/virtualagc/yaAGC"
    REQUIRES     agc_core
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE __embedded__)
```

- [ ] **Step 2: Create the header**

`components/peripheral_stub/include/peripheral_stub.h`:

```c
#pragma once
// peripheral_stub — watchdog for Luminary099 peripheral inputs.
//
// Luminary's IMU monitoring code (T4RUPT_PROGRAM.agc::T4JOB) reads
// channels 030/033 every mode-switch cycle, mirrors them into the
// IMODES30/IMODES33 erasable variables, and asserts PROG ALARM if any
// fault bits show. Without simulated CDU counters / radar this happens
// constantly. peripheral_stub_tick() restores those channels to
// healthy baselines and rewrites IMODES30/33 with fresh-start values
// on every tick, breaking the alarm-ack loop at idle.
//
// Called from channel_router_on_routine() at ~10 Hz wall-time.
//
// This is option (b) from
//   docs/superpowers/specs/2026-05-10-prog-alarm-watchdog-design.md
// Option (c) — actual CDU/radar simulation needed to run P63 descent —
// is deferred.

#include "yaAGC.h"
#include "agc_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

void peripheral_stub_init(void);
void peripheral_stub_tick(agc_t *state);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 3: Create the stub implementation (no-op for now)**

`components/peripheral_stub/peripheral_stub.c`:

```c
// peripheral_stub.c — implementation. See peripheral_stub.h for rationale.

#include "peripheral_stub.h"

void peripheral_stub_init(void) { }
void peripheral_stub_tick(agc_t *state) { (void)state; }
```

- [ ] **Step 4: Verify the component builds in the ESP-IDF tree**

```
idf.py reconfigure
```

Expected: completes without errors. The component appears in the CMake configure log.

(If you don't have IDF env set up in this session, skip — the host test in Task 5 will exercise the component anyway.)

- [ ] **Step 5: Commit**

```
git add components/peripheral_stub
git commit -m "peripheral_stub: scaffold component (stub tick function)"
```

---

## Task 5: Write the failing host test for peripheral_stub

**Files:**
- Create: `tests/host/test_peripheral_stub_clears_imodes.c`
- Modify: `tests/host/Makefile` (add peripheral_stub.c to HARNESS_SRCS, add new include dir, add test to LAYER2_TESTS)

- [ ] **Step 1: Create the test**

`tests/host/test_peripheral_stub_clears_imodes.c`:

```c
// test_peripheral_stub_clears_imodes — verifies that peripheral_stub_tick()
// restores ch030/ch033 baselines and rewrites IMODES30/IMODES33 to
// their fresh-start values, regardless of what fault bits were set
// before the tick.
//
// See docs/superpowers/specs/2026-05-10-prog-alarm-watchdog-design.md.

#include "agc_harness.h"
#include "peripheral_stub.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"

#include <stdio.h>

extern agc_t *agc_core_state(void);

#define CH030_BASELINE   036377
#define CH033_BASELINE   077777
#define IMODES30_FRESH   037411
#define IMODES33_FRESH   016000
#define IMODES30_BANK    2
#define IMODES30_OFFSET  0302
#define IMODES33_BANK    2
#define IMODES33_OFFSET  0303

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    // Stomp the channels and erasable mirrors with arbitrary fault
    // patterns to simulate Luminary having just written fault bits.
    st->InputChannel[030] = 000000;   // all "signal present" (everything failing)
    st->InputChannel[033] = 000000;   // AGC WARNING + PIPA FAIL + ...
    st->Erasable[IMODES30_BANK][IMODES30_OFFSET] = 077777;
    st->Erasable[IMODES33_BANK][IMODES33_OFFSET] = 077777;

    peripheral_stub_tick(st);

    printf("after tick: ch030=%06o ch033=%06o imodes30=%06o imodes33=%06o\n",
           st->InputChannel[030], st->InputChannel[033],
           st->Erasable[IMODES30_BANK][IMODES30_OFFSET],
           st->Erasable[IMODES33_BANK][IMODES33_OFFSET]);

    ASSERT(st->InputChannel[030] == CH030_BASELINE,
        "ch030 should be restored to CH030_BASELINE");
    ASSERT(st->InputChannel[033] == CH033_BASELINE,
        "ch033 should be restored to CH033_BASELINE");
    ASSERT(st->Erasable[IMODES30_BANK][IMODES30_OFFSET] == IMODES30_FRESH,
        "IMODES30 should be rewritten to fresh-start value");
    ASSERT(st->Erasable[IMODES33_BANK][IMODES33_OFFSET] == IMODES33_FRESH,
        "IMODES33 should be rewritten to fresh-start value");

    PASS();
}
```

- [ ] **Step 2: Update Makefile to include peripheral_stub**

In `tests/host/Makefile`:

**(a)** Add a new variable below the existing `CH_ROUTER` line (around line 31):

```
PSTUB    := $(ROOT)/components/peripheral_stub
```

**(b)** Update `HARNESS_INC` (line ~45) to add the new include dir. Replace:

```
HARNESS_INC  := -I$(YAAGC) -I$(AGC_CORE)/include -I$(CH_ROUTER)/include \
                -Iinclude -I.
```

with:

```
HARNESS_INC  := -I$(YAAGC) -I$(AGC_CORE)/include -I$(CH_ROUTER)/include \
                -I$(PSTUB)/include -Iinclude -I.
```

**(c)** Update `HARNESS_SRCS` (line ~47) to add the new source. Replace:

```
HARNESS_SRCS := \
    $(ENGINE_SRCS) \
    $(INIT_SRCS) \
    $(AGC_CORE)/io_callbacks.c \
    $(CH_ROUTER)/channel_router.c \
    agc_harness.c
```

with:

```
HARNESS_SRCS := \
    $(ENGINE_SRCS) \
    $(INIT_SRCS) \
    $(AGC_CORE)/io_callbacks.c \
    $(CH_ROUTER)/channel_router.c \
    $(PSTUB)/peripheral_stub.c \
    agc_harness.c
```

**(d)** Extend `LAYER2_TESTS` (line ~55). Replace (after Task 2's edit):

```
LAYER2_TESTS := test_alarm_at_boot test_p00_select test_lamp_test \
                test_rset_clears_alarms test_replay_apollo11_launch \
                test_auto_rset_at_boot
```

with:

```
LAYER2_TESTS := test_alarm_at_boot test_p00_select test_lamp_test \
                test_rset_clears_alarms test_replay_apollo11_launch \
                test_auto_rset_at_boot test_peripheral_stub_clears_imodes
```

- [ ] **Step 3: Run the test — expect FAIL**

```
cd tests/host
mingw32-make run-test_peripheral_stub_clears_imodes
```

Expected: FAIL on the first ASSERT — `ch030 should be restored to CH030_BASELINE` — because `peripheral_stub_tick()` is still a no-op.

- [ ] **Step 4: Commit (test only)**

```
git add tests/host/test_peripheral_stub_clears_imodes.c tests/host/Makefile
git commit -m "tests/host: failing test for peripheral_stub watchdog"
```

---

## Task 6: Implement peripheral_stub_tick body

**Files:**
- Modify: `components/peripheral_stub/peripheral_stub.c`

- [ ] **Step 1: Fill in the implementation**

Replace the entire contents of `components/peripheral_stub/peripheral_stub.c` with:

```c
// peripheral_stub.c — implementation. See peripheral_stub.h for rationale.
//
// Approach: every tick, idempotently re-assign the peripheral input
// channels Luminary monitors, and rewrite the IMODES30/IMODES33
// erasable mirrors to fresh-start values. Luminary's T4JOB will pick
// these up on its next mode-switch cycle and find no faults.
//
// Address derivation: MAIN.agc.html shows IMODES30 @ octal 01302,
// IMODES33 @ 01303. yaAGC's agc_t::Erasable[8][0400] indexes by
// (addr / 0400, addr % 0400) -> bank 2, offsets 0302 / 0303.
//
// Fresh-start values come from T4RUPT_PROGRAM.agc comments:
//   IMODES30 = 037411 (line 273)
//   IMODES33 = 016000 (line 527)
//
// Channel baselines match agc_init.c::init_cpu_state():
//   ch030 = 036377 (healthy LM)
//   ch033 = 077777 (no AGC warning)

#include "peripheral_stub.h"

#define CH030_BASELINE   036377
#define CH033_BASELINE   077777
#define IMODES30_BANK    2
#define IMODES30_OFFSET  0302
#define IMODES30_FRESH   037411
#define IMODES33_BANK    2
#define IMODES33_OFFSET  0303
#define IMODES33_FRESH   016000

void peripheral_stub_init(void)
{
    // Nothing to do at init; tick is idempotent and self-correcting.
}

void peripheral_stub_tick(agc_t *state)
{
    if (state == NULL) return;

    // Restore peripheral channel baselines (ch031 and ch032 carry
    // stick / PRO-key state and are intentionally left alone).
    state->InputChannel[030] = CH030_BASELINE;
    state->InputChannel[033] = CH033_BASELINE;

    // Restore IMODES30/IMODES33 to fresh-start values so any fault
    // bits Luminary set on its last mode-switch pass go away before
    // the next ALARM check runs.
    state->Erasable[IMODES30_BANK][IMODES30_OFFSET] = IMODES30_FRESH;
    state->Erasable[IMODES33_BANK][IMODES33_OFFSET] = IMODES33_FRESH;
}
```

- [ ] **Step 2: Run the test — expect PASS**

```
cd tests/host
mingw32-make run-test_peripheral_stub_clears_imodes
```

Expected: `after tick: ch030=036377 ch033=077777 imodes30=037411 imodes33=016000` and `PASS`.

- [ ] **Step 3: Commit**

```
git add components/peripheral_stub/peripheral_stub.c
git commit -m "peripheral_stub: implement tick (restore channels + IMODES30/33)"
```

---

## Task 7: Wire peripheral_stub_tick into channel_router_on_routine

**Files:**
- Modify: `components/channel_router/channel_router.c`
- Modify: `components/channel_router/CMakeLists.txt`

- [ ] **Step 1: Add peripheral_stub include + call**

In `components/channel_router/channel_router.c`, add this include near the other component-header includes (alongside `dsky_keys.h`):

```c
#include "peripheral_stub.h"
```

Then in `channel_router_on_routine()`, add the tick call as the **first line** of the function (before the `g_routine_count++;` line from Task 3):

```c
    peripheral_stub_tick(agc_core_state());
```

Note: `agc_core_state()` is already declared `extern agc_t *agc_core_state(void);` later in the same function (Task 3 didn't change that). Move that extern declaration to the top of the file (just below the existing `#include "agc_engine.h"`) so it's in scope here:

Find:
```c
    extern agc_t *agc_core_state(void);
    agc_t *st = agc_core_state();
```

(currently around line 293-294). Delete the `extern` line. Then add at the top of the file (around line 18, near the other includes):

```c
extern agc_t *agc_core_state(void);
```

The diagnostic snapshot block lower in the function can still use `st = agc_core_state();` — just remove the now-redundant `extern` keyword inline.

- [ ] **Step 2: Add peripheral_stub to channel_router's CMake REQUIRES**

In `components/channel_router/CMakeLists.txt`, change `REQUIRES freertos` to:

```cmake
idf_component_register(
    SRCS         "channel_router.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../third_party/virtualagc/yaAGC"
    REQUIRES     freertos
    PRIV_REQUIRES peripheral_stub
)
```

- [ ] **Step 3: Run the full host test suite**

```
cd tests/host
mingw32-make run
```

Expected: 13/13 tests PASS:

```
--- test_rom_load ---           PASS
--- test_engine_boot ---        PASS
--- test_channel10_emit ---     PASS
--- test_keypad_hit ---         PASS
--- test_alarm_at_boot ---      PASS
--- test_p00_select ---         PASS
--- test_lamp_test ---          PASS
--- test_rset_clears_alarms --- PASS
--- test_replay_apollo11_launch --- PASS
--- test_auto_rset_at_boot ---  PASS
--- test_peripheral_stub_clears_imodes --- PASS
--- test_render_blank ---       PASS
--- test_render_prog_lit ---    PASS
ALL PASS
```

If `test_rset_clears_alarms` regresses now (the harness's manual-RSET path may see IMODES already rewritten, changing the prog_alarm visibility) — it shouldn't fail the assertion (which is only on `restart`), but the diagnostic `prog_alarm` print may now show 0 where it previously showed 1. That's the intended improvement.

- [ ] **Step 4: Commit**

```
git add components/channel_router/channel_router.c components/channel_router/CMakeLists.txt
git commit -m "channel_router: wire peripheral_stub_tick into on_routine hook"
```

---

## Task 8: Hardware confirmation (manual)

This task is observational, not test-driven.

- [ ] **Step 1: Build firmware**

```
. C:\esp\v6.0.1\esp-idf\export.ps1
cd C:\Users\zombo\Desktop\Programming\espAGC
idf.py build
```

Expected: build completes, ~2 minutes cold cache.

- [ ] **Step 2: Flash + monitor**

```
idf.py -p COM<n> flash monitor
```

(Replace `COM<n>` with the dongle's port — e.g. `COM5`.)

- [ ] **Step 3: Observe boot behavior**

Watch the UART log for:

1. Within first ~1 s: chrouter logs ch010 row 12 `payload=0400` (PROG ALARM bit set), ch011 oscillating around `020000`/`020002` (COMP ACTY blink). Hardware lamps: RST + PROG steady yellow.
2. At ~5 s: log line `chrouter: auto-RSET posted at boot (tick 50)`.
3. Shortly after: ch010 row 12 should show `payload=0000` (alarm bit cleared). The next `chrouter: snap ...` dump should show `pa=0`.
4. From then on: alarm should stay off. The periodic `chrouter: alarms RuptLock=... NW=... WF=...` line should show `WF=0` (warning filter calm).

- [ ] **Step 4: Document in session notes**

Append to `docs/SESSION_NOTES.md` a short paragraph reporting what was observed (alarm cleared timestamps, any unexpected re-alarms, whether `test_rset_clears_alarms` diagnostic now reports `pa=0` post-RSET). This is the empirical confirmation that the (a)+(b) combination is sufficient for idle. If V37E63E (P63) is attempted next, expect re-alarm — that's option (c)'s territory.

- [ ] **Step 5: Commit the notes update**

```
git add docs/SESSION_NOTES.md
git commit -m "docs: record hardware confirmation of PROG ALARM watchdog"
```

---

## Self-review (already run by author)

- **Spec coverage:** auto-RSET → Tasks 1-3. peripheral_stub component → Tasks 4-6. Integration → Task 7. Hardware confirmation → Task 8. Kconfig gate → Task 1. Tests 11 → 13 → Tasks 2, 5. All spec sections covered.
- **No placeholders:** every code block is complete; addresses (`Erasable[2][0302]`, `[2][0303]`) and fresh values (`037411`, `016000`) are concrete; no TBDs.
- **Type/name consistency:** `peripheral_stub_tick(agc_t *state)` signature is identical in header, implementation, and test. `CH030_BASELINE`/`CH033_BASELINE`/`IMODES30_FRESH`/`IMODES33_FRESH` macros are spelled the same wherever used. `DSKY_KEY_RSET` (value 18) comes from the existing `dsky_keys.h`.
- **TDD discipline:** every component change is preceded by a failing test (Tasks 2, 5). Auto-RSET implementation (Task 3) and tick body (Task 6) each have a runnable check.
