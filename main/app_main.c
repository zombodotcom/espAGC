// app_main.c — espAGC top-level glue.
//
//  +---------------------+   +---------------------+
//  | agc_task (core 1)   |   | ui_task (core 0)    |
//  | runs agc_core_step  |   | snapshots state and |
//  | engine writes go    |   | renders LVGL DSKY,  |
//  | through io_callbacks|   | drives status LED   |
//  +----------+----------+   +----------+----------+
//             │                          ▲
//             ▼                          │
//        channel_router  ◄───── snapshot read
//             ▲
//             │
//   +---------+----------+----------+
//   | usb_cdc | wifi_ap  | (button) |
//   +--------------------+----------+

#include "agc_core.h"
#include "apollo_rom.h"
#include "board_pins.h"
#include "channel_router.h"
#include "display_hal.h"
#include "dsky_input.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app";

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
    channel_router_init();
    display_hal_init();   // also brings up the ST7735 + APA102 silence path

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

    dsky_input_config_t in_cfg = {
        .enable_usb_cdc = true,
        .enable_wifi_ap = true,
        .wifi_ssid = "espAGC",
        .wifi_password = "",     // open network
    };
    dsky_input_start(&in_cfg);

    // ESP32-C5 is single-core ("Unicore app" in boot log) — no core 1 to
    // pin to. Use plain xTaskCreate; FreeRTOS picks the only core (0).
    xTaskCreate(agc_task, "agc", 6144, NULL, 10, NULL);
    xTaskCreate(ui_task,  "ui",  6144, NULL,  5, NULL);

    ESP_LOGI(TAG, "espAGC running");
}
