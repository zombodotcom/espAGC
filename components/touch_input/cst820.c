// components/touch_input/cst820.c
//
// CST820 capacitive touch driver for the CYD-2432S028C. I2C, 7-bit addr 0x15.
// Reports a single (x, y) point after a small reset pulse on the RST pin.
//
// Register 0x01: gesture/finger count (non-zero = touch). Registers 0x03..0x06:
// X/Y as two big-endian 12-bit values in native portrait orientation.

#include "cst820.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "cst820";

#define CST820_ADDR 0x15

static cst820_pins_t           s_pins;
static i2c_master_dev_handle_t s_dev;
static i2c_master_bus_handle_t s_bus;

esp_err_t cst820_init_with_pins(const cst820_pins_t *p)
{
    s_pins = *p;

    gpio_config_t rst = { .pin_bit_mask = 1ULL << s_pins.rst, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&rst);
    gpio_set_level(s_pins.rst, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(s_pins.rst, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1,
        .sda_io_num = s_pins.sda,
        .scl_io_num = s_pins.scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bus_cfg, &s_bus) != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed");
        return ESP_FAIL;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = CST820_ADDR,
        .scl_speed_hz    = 400000,
    };
    if (i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev) != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "CST820 ready (sda=%d scl=%d rst=%d)", s_pins.sda, s_pins.scl, s_pins.rst);
    return ESP_OK;
}

static esp_err_t cst820_read(uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, 100);
}

bool cst820_poll(int *x_out, int *y_out)
{
    uint8_t buf[6] = { 0 };
    if (cst820_read(0x01, buf, 6) != ESP_OK) return false;
    if (buf[0] == 0) return false;

    int x = ((buf[2] & 0x0F) << 8) | buf[3];
    int y = ((buf[4] & 0x0F) << 8) | buf[5];
    // Native CST820 reports 240(x) x 320(y) portrait. Rotate to landscape
    // so coordinates match the panel's MADCTL orientation:
    //   landscape_x = native_y, landscape_y = 240 - native_x
    *x_out = y;
    *y_out = 240 - x;
    return true;
}
