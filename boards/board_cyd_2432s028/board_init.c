// boards/board_cyd_2432s028/board_init.c
#include "board_pins.h"
#include "ili9341_panel.h"
#include "xpt2046.h"
#include "rgb_gpio.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"

static const char *TAG = "board";

static const ili9341_pins_t s_lcd_pins = {
    .sck            = BOARD_LCD_SCK,
    .mosi           = BOARD_LCD_MOSI,
    .miso           = BOARD_LCD_MISO,
    .cs             = BOARD_LCD_CS,
    .dc             = BOARD_LCD_DC,
    .rst            = BOARD_LCD_RST,
    .bl             = BOARD_LCD_BL,
    .bl_active_low  = BOARD_LCD_BL_ACTIVE_LOW,
};

static esp_err_t cyd_panel_init(void) { return ili9341_init(&s_lcd_pins); }

static const display_panel_iface_t s_panel = {
    .width      = BOARD_LCD_HRES,
    .height     = BOARD_LCD_VRES,
    .swap_bytes = true,
    .init       = cyd_panel_init,
    .draw_rows  = ili9341_draw_rows,
};

static esp_err_t cyd_touch_init(void)
{
    xpt2046_pins_t p = {
        .host       = SPI3_HOST,             // VSPI; LCD is on SPI2_HOST
        .sck        = BOARD_TOUCH_SCK,
        .mosi       = BOARD_TOUCH_MOSI,
        .miso       = BOARD_TOUCH_MISO,
        .cs         = BOARD_TOUCH_CS,
        .irq        = BOARD_TOUCH_IRQ,
        .swap_xy    = true,                  // panel is in landscape
        .invert_x   = false,
        .invert_y   = true,
        .panel_w    = BOARD_LCD_HRES,        // 320
        .panel_h    = BOARD_LCD_VRES,        // 240
    };
    return xpt2046_init_with_pins(&p);
}

static const panel_touch_iface_t s_touch = {
    .init = cyd_touch_init,
    .poll = xpt2046_poll,
};

static void cyd_led_init(void)
{
    rgb_gpio_pins_t p = {
        .r = BOARD_LED_R, .g = BOARD_LED_G, .b = BOARD_LED_B,
        .active_low = true,
    };
    rgb_gpio_init_with_pins(&p);
}

static const led_status_iface_t s_led = {
    .init    = cyd_led_init,
    .set_rgb = rgb_gpio_set_rgb,
};

const display_panel_iface_t *board_get_panel(void) { return &s_panel; }
const panel_touch_iface_t   *board_get_touch(void) { return &s_touch; }
const led_status_iface_t    *board_get_led(void)   { return &s_led; }

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
