// components/led_status/rgb_gpio.c
//
// 3-GPIO RGB LED driver. Each color channel toggles on/off at 0x80 — no
// PWM in v1. Used by the CYD's onboard RGB LED, which is wired as 3
// separate active-low GPIOs (4/16/17).

#include "rgb_gpio.h"
#include "driver/gpio.h"

static rgb_gpio_pins_t s_pins;

void rgb_gpio_init_with_pins(const rgb_gpio_pins_t *p)
{
    s_pins = *p;
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << s_pins.r) | (1ULL << s_pins.g) | (1ULL << s_pins.b),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cfg);
    rgb_gpio_set_rgb(0, 0, 0);
}

void rgb_gpio_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    int rh = (r >= 0x80) ? 1 : 0;
    int gh = (g >= 0x80) ? 1 : 0;
    int bh = (b >= 0x80) ? 1 : 0;
    int on  = s_pins.active_low ? 0 : 1;
    int off = s_pins.active_low ? 1 : 0;
    gpio_set_level(s_pins.r, rh ? on : off);
    gpio_set_level(s_pins.g, gh ? on : off);
    gpio_set_level(s_pins.b, bh ? on : off);
}
