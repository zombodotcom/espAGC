// components/touch_input/xpt2046.c
//
// XPT2046 resistive touchscreen driver. 12-bit ADC over SPI mode 0.
// Each measurement is a 3-byte transaction: 1 control byte out, 2 data
// bytes back (12 useful bits, MSB-first, left-aligned in the upper 12
// bits of the returned 16-bit value — we shift right by 3 to recover).
//
// Control byte layout (datasheet table 5):
//   bit 7    : start (always 1)
//   bits 6:4 : channel select   X=0b101 (0xD0) Y=0b001 (0x90) Z1=0b011 (0xB0)
//   bit 3    : mode (0=12-bit, 1=8-bit) -> 0
//   bit 2    : SER/DFR (0=differential ratiometric, 1=single-ended) -> 0
//   bits 1:0 : PD1/PD0 (power-down between conversions) -> 01 (PENIRQ enabled)
//
// Pressure detection: read several X/Y samples in a row. If they're stable
// AND the IRQ pin is low (when wired), the panel is being pressed. We use
// IRQ when wired (CYD wires it on GPIO36) — much cheaper than reading Z.

#include "xpt2046.h"

#include <string.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "xpt2046";

#define XPT_SPI_HZ      (1 * 1000 * 1000)   // 1 MHz: XPT2046 is slow

#define CMD_X           0xD0
#define CMD_Y           0x90

static spi_device_handle_t s_dev;
static xpt2046_pins_t      s_cfg;

static int read_channel(uint8_t cmd)
{
    // 3-byte transaction: [cmd, 0, 0] -> [_, hi, lo]
    uint8_t tx[3] = { cmd, 0, 0 };
    uint8_t rx[3] = { 0, 0, 0 };
    spi_transaction_t t = {
        .length = 24, .tx_buffer = tx, .rx_buffer = rx, .rxlength = 24,
    };
    if (spi_device_polling_transmit(s_dev, &t) != ESP_OK) return -1;
    int v = ((rx[1] << 8) | rx[2]) >> 3;     // 12-bit, left-aligned in 15 bits
    return v & 0x0FFF;
}

esp_err_t xpt2046_init_with_pins(const xpt2046_pins_t *p)
{
    s_cfg = *p;

    // Defaults if caller passed zero (caller may pre-fill instead)
    if (!s_cfg.raw_x_max) s_cfg.raw_x_max = 3900;
    if (!s_cfg.raw_x_min) s_cfg.raw_x_min = 200;
    if (!s_cfg.raw_y_max) s_cfg.raw_y_max = 3900;
    if (!s_cfg.raw_y_min) s_cfg.raw_y_min = 200;
    if (!s_cfg.panel_w)   s_cfg.panel_w   = 320;
    if (!s_cfg.panel_h)   s_cfg.panel_h   = 240;

    if (s_cfg.irq >= 0) {
        gpio_config_t irq = {
            .pin_bit_mask = 1ULL << s_cfg.irq,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
        };
        gpio_config(&irq);
    }

    spi_bus_config_t buscfg = {
        .mosi_io_num   = s_cfg.mosi,
        .miso_io_num   = s_cfg.miso,
        .sclk_io_num   = s_cfg.sck,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };
    if (spi_bus_initialize(s_cfg.host, &buscfg, SPI_DMA_DISABLED) != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed");
        return ESP_FAIL;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = XPT_SPI_HZ,
        .mode           = 0,
        .spics_io_num   = s_cfg.cs,
        .queue_size     = 1,
    };
    if (spi_bus_add_device(s_cfg.host, &devcfg, &s_dev) != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "XPT2046 ready (sck=%d mosi=%d miso=%d cs=%d irq=%d)",
             s_cfg.sck, s_cfg.mosi, s_cfg.miso, s_cfg.cs, s_cfg.irq);
    return ESP_OK;
}

static int map_range(int v, int in_min, int in_max, int out_max)
{
    if (in_max == in_min) return 0;
    int span = in_max - in_min;
    int s = ((v - in_min) * out_max) / span;
    if (s < 0) s = 0;
    if (s > out_max - 1) s = out_max - 1;
    return s;
}

bool xpt2046_poll(int *x_out, int *y_out)
{
    // Cheap path: if IRQ pin is wired and HIGH, no touch.
    if (s_cfg.irq >= 0 && gpio_get_level(s_cfg.irq) == 1) return false;

    // Read X and Y twice; if they're not roughly consistent, treat as no-touch
    // (filters out the noise spike that XPT2046 returns when no finger is down).
    int x1 = read_channel(CMD_X);
    int y1 = read_channel(CMD_Y);
    int x2 = read_channel(CMD_X);
    int y2 = read_channel(CMD_Y);
    if (x1 < 0 || y1 < 0 || x2 < 0 || y2 < 0) return false;

    int dx = x1 > x2 ? x1 - x2 : x2 - x1;
    int dy = y1 > y2 ? y1 - y2 : y2 - y1;
    if (dx > 50 || dy > 50) return false;
    int rx = (x1 + x2) / 2;
    int ry = (y1 + y2) / 2;

    // XPT2046 reports very low values (< raw_min) when no finger is on the
    // panel even with IRQ asserted briefly. Drop those.
    if (rx < s_cfg.raw_x_min || ry < s_cfg.raw_y_min) return false;

    int px, py;
    if (s_cfg.swap_xy) {
        px = map_range(ry, s_cfg.raw_y_min, s_cfg.raw_y_max, s_cfg.panel_w);
        py = map_range(rx, s_cfg.raw_x_min, s_cfg.raw_x_max, s_cfg.panel_h);
    } else {
        px = map_range(rx, s_cfg.raw_x_min, s_cfg.raw_x_max, s_cfg.panel_w);
        py = map_range(ry, s_cfg.raw_y_min, s_cfg.raw_y_max, s_cfg.panel_h);
    }
    if (s_cfg.invert_x) px = s_cfg.panel_w - 1 - px;
    if (s_cfg.invert_y) py = s_cfg.panel_h - 1 - py;

    *x_out = px;
    *y_out = py;
    return true;
}
