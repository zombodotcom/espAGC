// app_main.c — espAGC top-level glue.
//
//  +---------------------+   +---------------------+
//  | agc_task            |   | ui_task             |
//  | runs agc_core_step  |   | snapshots state and |
//  | engine writes go    |   | renders the 320x240 |
//  | through io_callbacks|   | DSKY framebuffer    |
//  +----------+----------+   +----------+----------+
//             │                          ▲
//             ▼                          │
//        channel_router  ◄───── snapshot read
//             ▲
//             │
//   +---------+----------+----------+
//   | touch   | wifi_ap  | (button) |
//   +--------------------+----------+

#include "agc_core.h"
#include "apollo_rom.h"
#include "board_pins.h"
#include "channel_router.h"
#include "display_hal.h"
#include "dsky_input.h"
#include "display_panel_iface.h"
#include "led_status_iface.h"
#include "peripheral_stub.h"
#include "touch_input.h"
#include "dsky_layout.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app";

extern const display_panel_iface_t *board_get_panel(void);
extern const panel_touch_iface_t   *board_get_touch(void);
extern const led_status_iface_t    *board_get_led(void);

static apollo_rom_id_t pick_rom(void)
{
    // Boot button held during reset → CSM (Comanche055). Default → LM.
    return gpio_get_level(BOARD_BUTTON_BOOT) == 0
        ? APOLLO_ROM_COMANCHE055
        : APOLLO_ROM_LUMINARY099;
}

static void agc_task(void *arg)
{
    (void)arg;
    for (;;) {
        agc_core_step(2000);
        // Cooperatively yield so other tasks on the same core run.
        vTaskDelay(1);
    }
}

static void ui_task(void *arg)
{
    (void)arg;
    dsky_state_t state;
    uint64_t last_gen = (uint64_t)-1;
    for (;;) {
        uint64_t gen = channel_router_snapshot(&state);
        if (gen != last_gen) {
            display_hal_update(&state);
            last_gen = gen;
        }
        vTaskDelay(pdMS_TO_TICKS(33));   // ~30 Hz
    }
}

void app_main(void)
{
    board_init();

    const led_status_iface_t *led = board_get_led();
    if (led) {
        led->init();
        led->set_rgb(0xFF, 0x80, 0x00);   // amber while booting
    }

    channel_router_init();
    display_hal_init();   // brings up the ST7789 panel + 320x240 layout

    apollo_rom_id_t rom_id = pick_rom();
    size_t rom_size = 0;
    const uint8_t *rom = apollo_rom_get(rom_id, &rom_size);
    ESP_LOGI(TAG, "loading ROM %s (%u bytes)", apollo_rom_name(rom_id),
             (unsigned)rom_size);

    int rc = agc_core_init(rom, rom_size);
    if (rc != 0) {
        ESP_LOGE(TAG, "agc_core_init failed: %d", rc);
        return;
    }

    // Initialize the peripheral simulator. Future stages of this will
    // continuously drive ch030/031/032/033 and push CDU counter pulses
    // (mimicking what LM_Simulator does on Pi/Linux). For now it's a
    // hook for that work.
    peripheral_stub_init();

    dsky_input_config_t in_cfg = {
        .enable_wifi_ap = true,
        .wifi_ssid = "espAGC",
        .wifi_password = "",     // open network
    };
    dsky_input_start(&in_cfg);

    const panel_touch_iface_t *touch = board_get_touch();
    if (touch) {
        const display_panel_iface_t *panel = board_get_panel();
        const dsky_layout_t *layout = dsky_layout_for(panel->width, panel->height);
        if (layout && layout->hit_test) {
            touch_input_start(touch, layout->hit_test);
        } else {
            ESP_LOGW(TAG, "touch present but layout has no hit_test — skipping");
        }
    }

    // ESP32 dual-core; let FreeRTOS distribute across cores by default.
    xTaskCreate(agc_task, "agc", 6144, NULL, 10, NULL);
    xTaskCreate(ui_task,  "ui",  6144, NULL,  5, NULL);

    ESP_LOGI(TAG, "espAGC running");
    if (led) led->set_rgb(0x00, 0x40, 0x00);   // dim green = healthy
}
