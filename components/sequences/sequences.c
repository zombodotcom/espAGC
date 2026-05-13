#include "sequences.h"
#include "channel_router.h"
#include "dsky_keys.h"

#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "seq";

#define KEY_GAP_MS 250

// --- canned sequences -------------------------------------------------

#define V  DSKY_KEY_VERB
#define N  DSKY_KEY_NOUN
#define E  DSKY_KEY_ENTR
#define P  DSKY_KEY_PRO
#define R  DSKY_KEY_RSET
#define C  DSKY_KEY_CLR
#define K  DSKY_KEY_KEYREL
#define PL DSKY_KEY_PLUS
#define MI DSKY_KEY_MINUS
#define D0 DSKY_KEY_0
#define D1 DSKY_KEY_1
#define D2 DSKY_KEY_2
#define D3 DSKY_KEY_3
#define D4 DSKY_KEY_4
#define D5 DSKY_KEY_5
#define D6 DSKY_KEY_6
#define D7 DSKY_KEY_7
#define D8 DSKY_KEY_8
#define D9 DSKY_KEY_9

static const uint8_t SEQ_LAMP_TEST [] = { V, D3, D5, E };
static const uint8_t SEQ_P00_IDLE  [] = { V, D3, D7, E, D0, D0, E };
static const uint8_t SEQ_P01_PRELAUNCH[] = { V, D3, D7, E, D0, D1, E };
static const uint8_t SEQ_P63_LANDING[] = { V, D3, D7, E, D6, D3, E };
static const uint8_t SEQ_DISP_TIME [] = { V, D1, D6, N, D3, D6, E };
static const uint8_t SEQ_DISP_ALARM[] = { V, D0, D5, N, D0, D9, E };
static const uint8_t SEQ_RSET      [] = { R };

// Apollo 11 landing — Armstrong/Aldrin DSKY keystroke timeline from the
// Lunar Surface Journal (https://www.hq.nasa.gov/alsj/a11/a11.landing.html)
// and the LGC keystroke log. The original descent ran ~12 minutes (PDI at
// 102:33:05 GET to touchdown at 102:45:39 GET); compressed here to ~30 s
// at KEY_GAP_MS=250 because real-time waits would block the runner task.
// Famous moments are tagged inline.
static const uint8_t SEQ_APOLLO11_LANDING[] = {
    R,                              // reset to known state before the run
    // --- T-0:30: Aldrin starts P63 (Braking Phase) -------------------
    V, D3, D7, E, D6, D3, E,        // V37 E 63 E — Powered Descent Initiation
    // P63 displays V06N62 (cross-range / hdot / altitude) automatically.
    // Aldrin monitors and Houston confirms throttle-up.
    V, D1, D6, N, D6, D3, E,        // V16N63E — keep V06N63 alive (LR-aided alt)
    // --- T+0:38 to T+0:42: 1201/1202 PROGRAM ALARMS ------------------
    // "Twelve oh one" — Executive Overflow caused by rendezvous-radar
    // leakage filling the AGC work queue. Armstrong: "Program alarm!"
    // Houston ("Bales/Garman"): "We're GO on that alarm." Aldrin PRO'd
    // them. Five alarms total over P63/P64.
    P, P, P, P, P,                  // PRO ×5 — acknowledge 1201/1202
    // --- T+0:44: switch monitor to V16N68 (LPD / forward vel / alt) --
    V, D1, D6, N, D6, D8, E,        // V16N68E — Aldrin's call-out NOUN
    // --- T+1:00: P64 (Approach Phase) auto-loaded at altitude < 7000 ft.
    // Armstrong uses RHC for LPD redesignations; the LGC log shows
    // mostly NOUN updates rather than VERB changes during this phase.
    // --- T+1:30: Armstrong switches to P66 manual (West Crater detour)
    V, D3, D7, E, D6, D6, E,        // V37E66E — P66, manual rate-of-descent
    // P66's "+" key slows descent by 1 ft/s per press, "-" speeds it up.
    // Armstrong slowed dramatically while flying over the boulders.
    PL, PL, PL, PL,                 // + + + + — slow descent over field
    MI, MI,                         // - - — pick up rate after clearing it
    // --- T+2:30: "Sixty seconds" / "Thirty seconds" fuel calls --------
    // No keystrokes here — DSKY was unchanged. Honour the moment with a
    // pad of zero-length: the runner doesn't wait beyond KEY_GAP_MS.
    // --- T+2:38: TOUCHDOWN. "Houston, Tranquility Base here. The Eagle
    // has landed." Engine cutoff was automatic (ENGINE STOP discrete).
    V, D3, D7, E, D6, D8, E,        // V37E68E — P68 Landing Confirmation
    P,                              // PROCEED — confirm landing state vec
    // --- post-landing: V06N43 monitors lunar surface position --------
    V, D0, D6, N, D4, D3, E,        // V06N43E — display latitude / longitude / alt
};

#define SEQ(arr) (arr), (int)(sizeof(arr) / sizeof((arr)[0]))

static const sequence_t TABLE[] = {
    { "Lamp test (V35E)",    "Light every status indicator briefly", SEQ(SEQ_LAMP_TEST)  },
    { "P00 idle (V37E00E)",  "Select background program 00",          SEQ(SEQ_P00_IDLE)   },
    { "P01 prelaunch (V37E01E)", "LM prelaunch initialization",       SEQ(SEQ_P01_PRELAUNCH) },
    { "P63 landing (V37E63E)", "LM landing braking phase (no IMU)",   SEQ(SEQ_P63_LANDING) },
    { "Apollo 11 landing transcript",
      "Armstrong/Aldrin DSKY keystroke timeline: PDI → 1201/1202 alarms → P66 manual → touchdown",
      SEQ(SEQ_APOLLO11_LANDING) },
    { "Display time (V16N36E)", "R1 shows mission elapsed time",      SEQ(SEQ_DISP_TIME)  },
    { "Display alarms (V05N09E)", "R1 shows latest program alarm",    SEQ(SEQ_DISP_ALARM) },
    { "Reset (RSET)",        "Clear OPR ERR / abandon partial entry", SEQ(SEQ_RSET)       },
};
#define TABLE_COUNT ((int)(sizeof(TABLE) / sizeof(TABLE[0])))

int               sequences_count(void)         { return TABLE_COUNT; }
const sequence_t *sequences_get(int i)          { return (i >= 0 && i < TABLE_COUNT) ? &TABLE[i] : NULL; }

// --- runner -----------------------------------------------------------

static SemaphoreHandle_t s_busy;     // taken while a sequence is running

static void runner_task(void *arg)
{
    int idx = (int)(intptr_t)arg;
    const sequence_t *s = sequences_get(idx);
    if (s) {
        ESP_LOGI(TAG, "running '%s' (%d keys)", s->name, s->key_count);
        for (int i = 0; i < s->key_count; i++) {
            channel_router_post_key(s->keys[i]);
            vTaskDelay(pdMS_TO_TICKS(KEY_GAP_MS));
        }
        ESP_LOGI(TAG, "done '%s'", s->name);
    }
    xSemaphoreGive(s_busy);
    vTaskDelete(NULL);
}

int sequences_run(int index)
{
    if (!sequences_get(index)) return -1;
    if (!s_busy) s_busy = xSemaphoreCreateBinary(), xSemaphoreGive(s_busy);
    if (xSemaphoreTake(s_busy, 0) != pdTRUE) return -2;     // already running
    if (xTaskCreate(runner_task, "seq", 3072,
                    (void *)(intptr_t)index, 4, NULL) != pdPASS) {
        xSemaphoreGive(s_busy);
        return -3;
    }
    return 0;
}
