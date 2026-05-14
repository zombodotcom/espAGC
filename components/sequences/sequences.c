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
// "Houston-uplinks V35E" — same DSKY effect as the regular lamp test, but
// delivered through UPRUPT (ch0173 CCC) instead of KEYRUPT1 (ch015). The
// difference is visible: the UPLINK ACTY lamp (top-left of the indicator
// panel) lights as each character arrives, exactly as it did when Mission
// Control drove the AGC during the actual mission.
static const uint8_t SEQ_UPLINK_LAMPTEST[] = { V, D3, D5, E };

// Apollo 11 PDI state vector via Mission Control uplink (V71 protocol).
//
// Wire format from UPDATE_PROGRAM.agc:122-138:
//
//   V71E 21E <ECADR=UPSVFLAG> <IDENTIFIER> <X> <Y> <Z> <Vx> <Vy> <Vz> <T> V33E
//
// V71 = CONTIGUOUS BLOCK UPDATE; II=21 means 21 components total
// starting at the ECADR. The first is UPSVFLAG (identifier), then 6
// DP words for position (X/Y/Z high+low), 6 DP for velocity, 2 DP for
// time = 1 + 6 + 6 + 2 + 6 misc = 21.
//
// Approximation used here for Apollo 11 PDI conditions (lunar sphere
// of influence, identifier=77775):
//
//   PDI altitude     : ~50,000 ft = 15,240 m
//   Lunar radius     : 1,737,400 m
//   R magnitude      : 1,752,640 m at perilune
//   V perpendicular  : 1,688 m/s (≈5,535 ft/s)
//
// Reference frame: simplified lunar-centered with LM at perilune in
// XY plane (Z = 0).
//
// CAUTION: Luminary stores position as DP B-29 lunar and velocity as
// DP B-7 lunar; getting the exact octal-encoded scaling right requires
// careful transcription against real Apollo 11 LSJ PAD data. This
// sequence uses placeholder patterns that exercise the V71 protocol
// end-to-end — exact display values in R1/R2/R3 will be off-scale
// until the proper PAD octals replace these. Iterate from the device's
// response.
//
// Pre-condition: P00 must be active (V71 rejected outside P00 on the LM
// per UPDATE_PROGRAM.agc:57). The sequence starts with V37E00E.
// Real Apollo 11 LM state vector ~10 minutes before PDI, sourced from
// the virtualagc canonical scenarios branch — Luminary099 core dump
// "Apollo 11 P63 Ignition Algorithm.core" (post-DOI, on landing
// trajectory). Values are RRECTLEM/VRECTLEM/TETLEM extracted from
// bank 3 offsets 0o226..0o243 of the core dump, written as-is into the
// V71 temporary slot at UPSVFLAG (ECADR 01501). After V33E commits,
// Luminary integrates these into the permanent RRECTLEM/VRECTLEM/TETLEM.
//
// State-vector identifier 77775 selects LEM lunar-SOI scaling:
//   RRECT — meters, scaled B-27   (NOT B-29; see Luminary069 PADLOADS:
//           "METERS, B-29 OR B-27 IF EARTH OR MOON")
//   VRECT — m/cs,   scaled B-5    ("M/CSEC, B-7 OR B-5 IF EARTH OR MOON")
//   TET   — csec,   scaled B-28
//
// Decoded sanity check (positive components inverted from 1's complement
// when sign bit is set, magnitudes combined as hi*2^14 + lo):
//   R = (-60 km, +1627 km, +696 km)   →  |R| ≈ 1770 km (lunar surface ≈ 1738 km)
//   V = (+1679, +22.5, +2.8) m/s      →  |V| ≈ 1679 m/s (lunar orbital ≈ 1.68 km/s)
//   TET ≈ 102:25:13 since AGC clock zero  → matches "T-10 before PDI"
//
// V71 component count math (UPDATE_PROGRAM.agc:250-259 + UPEND71:434-444):
//   3 ≤ II ≤ 20 strict; words stored at ECADR = COMPNUMB - 2 = II - 2
//   For state vector: 1 identifier + 6 R + 6 V + 2 TET = 15 → II = 17.
//   (The L099 doc on page 1388 saying "21E" is a transcription error;
//    II=21 fails the range check at OHWELL1+2.)
static const uint8_t SEQ_UPLINK_PDI_STATE[] = {
    // ----- Step 1: ensure P00 (V71 only accepted during P00 on LM) -----
    V, D3, D7, E, D0, D0, E,             // V37E00E
    // ----- Step 2: V71 CONTIGUOUS BLOCK UPDATE -----
    V, D7, D1, E,                        // V71E
    D1, D7, E,                           // II=17 (15 data words after ECADR)
    D0, D1, D5, D0, D1, E,               // ECADR = 01501 (UPSVFLAG)
    D7, D7, D7, D7, D5, E,               // IDENTIFIER 77775 (LEM lunar SOI)
    // RRECT — position DP triple, 3 components × 2 words = 6 entries
    D7, D7, D7, D7, D0, E,               // RRECT[0] X_hi = 077770 (≈ -60 km)
    D6, D4, D4, D4, D4, E,               // RRECT[1] X_lo = 064444
    D0, D0, D3, D0, D6, E,               // RRECT[2] Y_hi = 000306 (≈ +1627 km)
    D2, D3, D3, D4, D6, E,               // RRECT[3] Y_lo = 023346
    D0, D0, D1, D2, D5, E,               // RRECT[4] Z_hi = 000125 (≈ +696 km)
    D0, D0, D2, D2, D3, E,               // RRECT[5] Z_lo = 000223
    // VRECT — velocity DP triple, 3 components × 2 words = 6 entries
    D2, D0, D6, D2, D1, E,               // VRECT[0] Vx_hi = 020621 (≈ +1679 m/s)
    D0, D0, D2, D7, D5, E,               // VRECT[1] Vx_lo = 000275
    D0, D0, D1, D6, D3, E,               // VRECT[2] Vy_hi = 000163 (≈ +22.5 m/s)
    D0, D0, D7, D6, D5, E,               // VRECT[3] Vy_lo = 000765
    D0, D0, D0, D1, D6, E,               // VRECT[4] Vz_hi = 000016 (≈ +2.8 m/s)
    D0, D4, D5, D4, D5, E,               // VRECT[5] Vz_lo = 004545
    // TET — time DP, 1 component × 2 words = 2 entries
    D0, D4, D3, D1, D2, E,               // TET[0] T_hi = 004312 (≈ 102:25 GET)
    D1, D6, D1, D4, D0, E,               // TET[1] T_lo = 016140
    // ----- Step 3: V33 to commit -----
    V, D3, D3, E,                        // V33E (signal ready to store)
};

// =================================================================
// Apollo 11 PDI state-vector uplink — TEMPLATE FOR v0.2.0
// =================================================================
//
// The canonical Mission Control state-vector load (UPDATE_PROGRAM.agc:122-138)
// is V71 (CONTIGUOUS BLOCK UPDATE) with 21 components starting at UPSVFLAG.
// Address constants (from yaYUL listing of Luminary099):
//
//   UPSVFLAG ECADR = 01501   (state-vector update flag)
//   RN       ECADR = 01220   (R-vector, 6 cells, 3 components in DP)
//   VN       ECADR = 01226   (V-vector, 6 cells, 3 components in DP)
//
// Wire format (each XXXXX = 5-digit octal, each E = ENTR):
//
//   V71E                 # CONTIGUOUS BLOCK UPDATE
//   21E                  # II = 21 components (II-2 = 19 data items)
//   01501E               # AAAA = ECADR of UPSVFLAG
//   77775E               # IDENTIFIER: 77775 = LEM lunar sphere of influence
//   XXXXXE XXXXXE        # X position (DP)
//   XXXXXE XXXXXE        # Y position
//   XXXXXE XXXXXE        # Z position
//   XXXXXE XXXXXE        # X velocity
//   XXXXXE XXXXXE        # Y velocity
//   XXXXXE XXXXXE        # Z velocity
//   XXXXXE XXXXXE        # Time from AGC clock zero (DP)
//   V33E                 # Commit — Luminary stores to RN/VN/PIPTIME
//
// CRITICAL: V71 only accepted during P00 on the LM (and only P00, P02
// or fresh-start on CSM). Send V37E00E first to ensure P00 is active.
//
// What's missing to make this fly:
//   - Apollo 11 PDI initial position/velocity in Luminary's specific
//     B-29 (position) / B-7 (velocity) lunar scaling. Public via LSJ
//     pre-PDI pad records but needs careful transcription.
//   - REFSMMAT (uses similar V71 24-component sequence at ECADR REFSMMAT).
//     Without this the IMU doesn't know which way is "down".
//   - DAP coefficients (MASS, etc.) — separate P-axis autopilot pad load.
//
// The UPRUPT typing primitive (yaagc_socket_inject_uplink_key) is in
// place and verified end-to-end on hardware (probe_uprupt2.py). Once
// the PAD values land, this sequence is ~30 lines and ships v0.2.0.

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

#define SEQ(arr)      (arr), (int)(sizeof(arr) / sizeof((arr)[0])), 0
#define SEQ_UP(arr)   (arr), (int)(sizeof(arr) / sizeof((arr)[0])), SEQ_FLAG_UPLINK

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
    { "Houston uplinks V35E (UPRUPT)",
      "Same lamp test, but delivered via ch0173 UPRUPT (CCC-encoded) — watch UPLINK ACTY light",
      SEQ_UP(SEQ_UPLINK_LAMPTEST) },
    { "Houston uplinks Apollo 11 PDI state (V71)",
      "V71 17-component state vector update via UPRUPT (CCC-encoded). "
      "Real RRECT/VRECT/TET from virtualagc scenarios Apollo 11 P63 core dump "
      "(post-DOI, T-10 min before PDI). ~95 uplink chars, ~25 s. Use before V37E63E.",
      SEQ_UP(SEQ_UPLINK_PDI_STATE) },
};
#define TABLE_COUNT ((int)(sizeof(TABLE) / sizeof(TABLE[0])))

int               sequences_count(void)         { return TABLE_COUNT; }
const sequence_t *sequences_get(int i)          { return (i >= 0 && i < TABLE_COUNT) ? &TABLE[i] : NULL; }

// --- runner -----------------------------------------------------------

static SemaphoreHandle_t s_busy;     // taken while a sequence is running

// CCC-encoded UPRUPT (ch0173) injector — provided by components/yaagc_socket.
extern int yaagc_socket_inject_uplink_key(int code);

static void runner_task(void *arg)
{
    int idx = (int)(intptr_t)arg;
    const sequence_t *s = sequences_get(idx);
    if (s) {
        const int via_uplink = (s->flags & SEQ_FLAG_UPLINK) ? 1 : 0;
        ESP_LOGI(TAG, "running '%s' (%d keys, %s)", s->name, s->key_count,
                 via_uplink ? "UPRUPT/ch0173" : "KEYRUPT1/ch015");
        for (int i = 0; i < s->key_count; i++) {
            if (via_uplink) {
                yaagc_socket_inject_uplink_key(s->keys[i]);
            } else {
                channel_router_post_key(s->keys[i]);
            }
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
