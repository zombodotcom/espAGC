// components/display_hal/display_hal.c
//
// Driver-agnostic glue. Looks up the panel iface (from the board) and the
// matching DSKY layout (from resolution), allocates a single strip-sized
// scratch buffer, and pushes frames in `strip_h`-row passes.

#include "display_hal.h"
#include "display_panel_iface.h"
#include "dsky_layout.h"

#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

extern const display_panel_iface_t *board_get_panel(void);

static const char *TAG = "dsky";

static const display_panel_iface_t *s_panel;
static const dsky_layout_t         *s_layout;
static uint16_t                    *s_strip;        // fb_w * strip_h pixels

void display_hal_init(void)
{
    s_panel = board_get_panel();
    if (!s_panel) {
        ESP_LOGE(TAG, "board returned no panel");
        return;
    }
    s_layout = dsky_layout_for(s_panel->width, s_panel->height);
    if (!s_layout) {
        ESP_LOGE(TAG, "no DSKY layout for %dx%d", s_panel->width, s_panel->height);
        return;
    }

    size_t bytes = (size_t)s_panel->width * s_layout->strip_h * sizeof(uint16_t);
    s_strip = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_strip) s_strip = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    if (!s_strip) { ESP_LOGE(TAG, "strip alloc (%u B) failed", (unsigned)bytes); return; }

    if (s_panel->init() != ESP_OK) { ESP_LOGE(TAG, "panel init failed"); return; }

    // Splash: walk strips, init each, push.
    for (int y0 = 0; y0 < s_layout->fb_h; y0 += s_layout->strip_h) {
        s_layout->init_strip(s_strip, y0);
        s_panel->draw_rows(y0, y0 + s_layout->strip_h, s_strip);
    }

    ESP_LOGI(TAG, "display_hal up: %dx%d, strip_h=%d",
             s_panel->width, s_panel->height, s_layout->strip_h);
}

void display_hal_update(const dsky_state_t *s)
{
    if (!s_strip || !s_panel || !s_layout) return;

    for (int y0 = 0; y0 < s_layout->fb_h; y0 += s_layout->strip_h) {
        s_layout->render_strip(s_strip, s, y0);
        s_panel->draw_rows(y0, y0 + s_layout->strip_h, s_strip);
    }
}
