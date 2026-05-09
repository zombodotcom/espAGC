// boards/board_cyd_2432s028/board_init.c
#include "board_pins.h"
#include "ili9341_panel.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "board";

static const ili9341_pins_t s_lcd_pins = {
    .sck  = BOARD_LCD_SCK,
    .mosi = BOARD_LCD_MOSI,
    .miso = BOARD_LCD_MISO,
    .cs   = BOARD_LCD_CS,
    .dc   = BOARD_LCD_DC,
    .rst  = BOARD_LCD_RST,
    .bl   = BOARD_LCD_BL,
};

static esp_err_t cyd_panel_init(void) { return ili9341_init(&s_lcd_pins); }

static const display_panel_iface_t s_panel = {
    .width      = BOARD_LCD_HRES,
    .height     = BOARD_LCD_VRES,
    .swap_bytes = true,
    .init       = cyd_panel_init,
    .draw_rows  = ili9341_draw_rows,
};

const display_panel_iface_t *board_get_panel(void) { return &s_panel; }
const panel_touch_iface_t   *board_get_touch(void) { return NULL; }   // T15
const led_status_iface_t    *board_get_led(void)   { return NULL; }   // T16

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
