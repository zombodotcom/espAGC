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

#define SEQ(arr) (arr), (int)(sizeof(arr) / sizeof((arr)[0]))

static const sequence_t TABLE[] = {
    { "Lamp test (V35E)",    "Light every status indicator briefly", SEQ(SEQ_LAMP_TEST)  },
    { "P00 idle (V37E00E)",  "Select background program 00",          SEQ(SEQ_P00_IDLE)   },
    { "P01 prelaunch (V37E01E)", "LM prelaunch initialization",       SEQ(SEQ_P01_PRELAUNCH) },
    { "P63 landing (V37E63E)", "LM landing braking phase (no IMU)",   SEQ(SEQ_P63_LANDING) },
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
