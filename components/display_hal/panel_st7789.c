// components/display_hal/panel_st7789.c
//
// ST7789 panel driver for the ESP32-2432S028 ("CYD2USB" 2.8" CYD).
// 240x320 native, rotated to 320x240 landscape via MADCTL. SPI2 host,
// 40 MHz. Backlight active-high on a separate GPIO.
//
// The CYD2USB-revision board uses an ST7789, not an ILI9341 like the
// older CYDs (per third_party/CYD-reference/cyd.md and DisplayConfig/
// CYD2USB/User_Setup.h). ST7789 shares the basic command set with ILI9341
// (SWRESET, SLPOUT, MADCTL, COLMOD, CASET, PASET, RAMWR, DISPON) but the
// power/gamma command space is different and ST7789 panels need INVON
// (display inversion) for correct colors — without it, the screen is
// either dead-looking or colour-inverted.
//
// References:
//   - third_party/CYD-reference/DisplayConfig/CYD2USB/User_Setup.h
//   - ST7789 datasheet, Sitronix v1.4
//   - LovyanGFX/Bodmer TFT_eSPI ST7789 init

#include "st7789_panel.h"

#include <string.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"

#define ST_HOST     SPI2_HOST
#define ST_SPI_HZ   (40 * 1000 * 1000)

#define ST_W  320
#define ST_H  240

#define ST_NOP        0x00
#define ST_SWRESET    0x01
#define ST_SLPOUT     0x11
#define ST_NORON      0x13
#define ST_INVOFF     0x20
#define ST_INVON      0x21
#define ST_DISPON     0x29
#define ST_CASET      0x2A
#define ST_PASET      0x2B
#define ST_RAMWR      0x2C
#define ST_MADCTL     0x36
#define ST_COLMOD     0x3A

// MADCTL: MV=1, MX=1, BGR=0 (RGB on this panel) -> 320x240 landscape.
// Per witnessmenow CYD2USB User_Setup.h, RGB order is BGR — but the BGR
// flag in MADCTL bit 3 means "the panel expects BGR data". This panel is
// wired such that we send BGR; the bit is therefore SET. Adjust if reds
// look blue or vice versa.
#define MADCTL_LANDSCAPE 0x68    // MV(0x20) | MX(0x40) | BGR(0x08)

static const char *TAG = "st7789";

static spi_device_handle_t s_spi;
static st7789_pins_t       s_pins;

static void cs_low (void) { gpio_set_level(s_pins.cs, 0); }
static void cs_high(void) { gpio_set_level(s_pins.cs, 1); }
static void dc_low (void) { gpio_set_level(s_pins.dc, 0); }
static void dc_high(void) { gpio_set_level(s_pins.dc, 1); }

static void spi_tx(const uint8_t *bytes, size_t len)
{
    if (!len) return;
    spi_transaction_t t = { .length = len * 8, .tx_buffer = bytes };
    cs_low();
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
    cs_high();
}
static void wcmd(uint8_t cmd)                   { dc_low();  spi_tx(&cmd, 1); }
static void wdat(const uint8_t *d, size_t n)    { dc_high(); spi_tx(d, n); }

static void send_init(void)
{
    wcmd(ST_SWRESET);  vTaskDelay(pdMS_TO_TICKS(150));
    wcmd(ST_SLPOUT);   vTaskDelay(pdMS_TO_TICKS(120));
    wcmd(ST_COLMOD);   wdat((uint8_t[]){0x55}, 1);          // RGB565
    wcmd(ST_MADCTL);   wdat((uint8_t[]){MADCTL_LANDSCAPE}, 1);
    wcmd(ST_INVON);    vTaskDelay(pdMS_TO_TICKS(10));       // critical for ST7789
    wcmd(ST_NORON);    vTaskDelay(pdMS_TO_TICKS(10));
    wcmd(ST_DISPON);   vTaskDelay(pdMS_TO_TICKS(120));
}

static void set_window(int x0, int y0, int x1, int y1)
{
    uint8_t c[4] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
    wcmd(ST_CASET); wdat(c, 4);
    uint8_t r[4] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };
    wcmd(ST_PASET); wdat(r, 4);
    wcmd(ST_RAMWR);
}

esp_err_t st7789_init(const st7789_pins_t *pins)
{
    s_pins = *pins;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << s_pins.cs) | (1ULL << s_pins.dc),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio_config CS/DC");
    cs_high();

    spi_bus_config_t buscfg = {
        .mosi_io_num = s_pins.mosi,
        .miso_io_num = s_pins.miso,
        .sclk_io_num = s_pins.sck,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = ST_W * 80 * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(ST_HOST, &buscfg, SPI_DMA_CH_AUTO),
                        TAG, "spi_bus_initialize");

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = ST_SPI_HZ, .mode = 0, .spics_io_num = -1,
        .queue_size = 4, .flags = SPI_DEVICE_NO_DUMMY,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(ST_HOST, &devcfg, &s_spi),
                        TAG, "spi_bus_add_device");

    send_init();

    gpio_config_t bl = { .pin_bit_mask = 1ULL << s_pins.bl, .mode = GPIO_MODE_OUTPUT };
    ESP_RETURN_ON_ERROR(gpio_config(&bl), TAG, "gpio_config BL");
    gpio_set_level(s_pins.bl, s_pins.bl_active_low ? 0 : 1);

    ESP_LOGI(TAG, "ST7789 ready: %dx%d landscape (BL gpio=%d %s)",
             ST_W, ST_H, s_pins.bl,
             s_pins.bl_active_low ? "active-low" : "active-high");
    return ESP_OK;
}

esp_err_t st7789_draw_rows(int y0, int y1, const uint16_t *pixels)
{
    if (y0 < 0 || y1 > ST_H || y1 <= y0) return ESP_ERR_INVALID_ARG;

    set_window(0, y0, ST_W - 1, y1 - 1);

    static uint16_t scratch[ST_W];
    int rows = y1 - y0;
    dc_high(); cs_low();
    for (int y = 0; y < rows; y++) {
        const uint16_t *row = &pixels[y * ST_W];
        for (int x = 0; x < ST_W; x++) {
            uint16_t p = row[x];
            scratch[x] = (uint16_t)((p << 8) | (p >> 8));
        }
        spi_transaction_t t = { .length = ST_W * 16, .tx_buffer = scratch };
        ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
    }
    cs_high();
    return ESP_OK;
}
