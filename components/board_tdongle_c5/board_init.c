#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "board";

void board_init(void)
{
    gpio_config_t btn = {
        .pin_bit_mask = 1ULL << BOARD_BUTTON_BOOT,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn);

    ESP_LOGI(TAG, "%s board init complete", BOARD_NAME);
}
