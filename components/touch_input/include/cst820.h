// components/touch_input/include/cst820.h
#pragma once
#include <stdbool.h>
#include "esp_err.h"

typedef struct { int sda, scl, rst, intr; } cst820_pins_t;

esp_err_t cst820_init_with_pins(const cst820_pins_t *p);
bool      cst820_poll(int *x_out, int *y_out);
