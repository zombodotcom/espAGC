// components/touch_input/touch_input.c
//
// 50 Hz touch poll task with edge-trigger + 80 ms debounce. On a
// transition from "released" to "pressed", looks up the cell at (x, y)
// via the layout's hit_test and posts the decoded key into channel_router.

#include "touch_input.h"
#include "channel_router.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "touch";

static const panel_touch_iface_t *s_touch;
static touch_hit_test_fn          s_hit_test;

static void touch_task(void *arg)
{
    (void)arg;
    bool last_pressed = false;
    int64_t last_emit_us = 0;

    for (;;) {
        int x = 0, y = 0;
        bool pressed = s_touch->poll(&x, &y);
        int64_t now = esp_timer_get_time();
        if (pressed && !last_pressed && (now - last_emit_us) > 80000) {
            int code = s_hit_test(x, y);
            if (code >= 0) {
                channel_router_post_key(code);
                last_emit_us = now;
                ESP_LOGI(TAG, "tap (%d,%d) -> key %d", x, y, code);
            }
        }
        last_pressed = pressed;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void touch_input_start(const panel_touch_iface_t *touch, touch_hit_test_fn hit_test)
{
    if (!touch || !hit_test) {
        ESP_LOGW(TAG, "touch_input disabled (touch=%p hit_test=%p)", touch, (void *)hit_test);
        return;
    }
    s_touch = touch;
    s_hit_test = hit_test;

    if (s_touch->init() != ESP_OK) {
        ESP_LOGE(TAG, "touch init failed");
        return;
    }
    xTaskCreate(touch_task, "touch", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "touch_input task up");
}
