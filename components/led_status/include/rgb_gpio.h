#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct { int r, g, b; bool active_low; } rgb_gpio_pins_t;

void rgb_gpio_init_with_pins(const rgb_gpio_pins_t *p);
void rgb_gpio_set_rgb(uint8_t r, uint8_t g, uint8_t b);
