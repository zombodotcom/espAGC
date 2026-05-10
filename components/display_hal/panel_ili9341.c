// components/display_hal/panel_ili9341.c
//
// ILI9341 panel driver for the CYD-2432S028C. 240x320 native, rotated to
// 320x240 landscape via MADCTL. SPI2 host, 40 MHz. Backlight active-high
// on a separate GPIO.
//
// References: ILI9341 datasheet rev1.11, Adafruit_ILI9341 library, the
// TFT_eSPI driver settings used by the CYD reference repo.

#include "ili9341_panel.h"

#include <string.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"

#define ILI_HOST     SPI2_HOST
#define ILI_SPI_HZ   (40 * 1000 * 1000)

#define ILI_W  320
#define ILI_H  240

#define ILI_SWRESET   0x01
#define ILI_SLPOUT    0x11
#define ILI_DISPON    0x29
#define ILI_CASET     0x2A
#define ILI_PASET     0x2B
#define ILI_RAMWR     0x2C
#define ILI_MADCTL    0x36
#define ILI_PIXFMT    0x3A
#define ILI_FRMCTR1   0xB1
#define ILI_DFUNCTR   0xB6
#define ILI_PWCTR1    0xC0
#define ILI_PWCTR2    0xC1
#define ILI_VMCTR1    0xC5
#define ILI_VMCTR2    0xC7
#define ILI_GMCTRP1   0xE0
#define ILI_GMCTRN1   0xE1
#define ILI_PWCTRA    0xCB
#define ILI_PWCTRB    0xCF
#define ILI_TIMCTRA   0xE8
#define ILI_TIMCTRB   0xEA
#define ILI_PWRSEQ    0xED
#define ILI_3GAMMAEN  0xF2
#define ILI_PUMPRC    0xF7
#define ILI_GAMMASET  0x26

// MADCTL: MX=1, MV=1, BGR=1 -> 320x240 landscape, BGR order. Matches the
// witnessmenow CYD reference (third_party/CYD-reference) for the C variant.
// If on-bringup the image looks horizontally flipped, try 0x28 (drop MX);
// if vertically flipped, try 0xE8 (add MY); if rotated 180, try 0xA8.
#define MADCTL_LANDSCAPE 0x68

static const char *TAG = "ili9341";

static spi_device_handle_t s_spi;
static ili9341_pins_t      s_pins;

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

// Full ILI9341 init sequence — power, gamma, frame rate, display function.
// The minimal SWRESET/SLPOUT/MADCTL/DISPON path "succeeds" at the SPI level
// but leaves the panel in a low-contrast / dark state on many ILI9341 dies.
// This is the canonical Adafruit_ILI9341 begin() sequence (which TFT_eSPI,
// LovyanGFX, and ESP Marauder all use). It reliably brings the CYD panel
// up to full brightness and contrast.
static void send_init(void)
{
    wcmd(ILI_SWRESET); vTaskDelay(pdMS_TO_TICKS(120));

    wcmd(ILI_PWCTRB);   wdat((uint8_t[]){0x00, 0xC1, 0x30}, 3);
    wcmd(ILI_PWRSEQ);   wdat((uint8_t[]){0x64, 0x03, 0x12, 0x81}, 4);
    wcmd(ILI_TIMCTRA);  wdat((uint8_t[]){0x85, 0x00, 0x78}, 3);
    wcmd(ILI_PWCTRA);   wdat((uint8_t[]){0x39, 0x2C, 0x00, 0x34, 0x02}, 5);
    wcmd(ILI_PUMPRC);   wdat((uint8_t[]){0x20}, 1);
    wcmd(ILI_TIMCTRB);  wdat((uint8_t[]){0x00, 0x00}, 2);

    wcmd(ILI_PWCTR1);   wdat((uint8_t[]){0x23}, 1);             // VRH[5:0]
    wcmd(ILI_PWCTR2);   wdat((uint8_t[]){0x10}, 1);             // SAP[2:0]; BT[3:0]
    wcmd(ILI_VMCTR1);   wdat((uint8_t[]){0x3E, 0x28}, 2);
    wcmd(ILI_VMCTR2);   wdat((uint8_t[]){0x86}, 1);

    wcmd(ILI_MADCTL);   wdat((uint8_t[]){MADCTL_LANDSCAPE}, 1);
    wcmd(ILI_PIXFMT);   wdat((uint8_t[]){0x55}, 1);             // RGB565
    wcmd(ILI_FRMCTR1);  wdat((uint8_t[]){0x00, 0x18}, 2);       // 79 Hz
    wcmd(ILI_DFUNCTR);  wdat((uint8_t[]){0x08, 0x82, 0x27}, 3);

    wcmd(ILI_3GAMMAEN); wdat((uint8_t[]){0x00}, 1);
    wcmd(ILI_GAMMASET); wdat((uint8_t[]){0x01}, 1);
    wcmd(ILI_GMCTRP1);  wdat((uint8_t[]){0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08,
                                         0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03,
                                         0x0E, 0x09, 0x00}, 15);
    wcmd(ILI_GMCTRN1);  wdat((uint8_t[]){0x00, 0x0E, 0x14, 0x03, 0x11, 0x07,
                                         0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C,
                                         0x31, 0x36, 0x0F}, 15);

    wcmd(ILI_SLPOUT);   vTaskDelay(pdMS_TO_TICKS(120));
    wcmd(ILI_DISPON);   vTaskDelay(pdMS_TO_TICKS(20));
}

static void set_window(int x0, int y0, int x1, int y1)
{
    uint8_t c[4] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
    wcmd(ILI_CASET); wdat(c, 4);
    uint8_t r[4] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };
    wcmd(ILI_PASET); wdat(r, 4);
    wcmd(ILI_RAMWR);
}

esp_err_t ili9341_init(const ili9341_pins_t *pins)
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
        .max_transfer_sz = ILI_W * 80 * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(ILI_HOST, &buscfg, SPI_DMA_CH_AUTO),
                        TAG, "spi_bus_initialize");

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = ILI_SPI_HZ, .mode = 0, .spics_io_num = -1,
        .queue_size = 4, .flags = SPI_DEVICE_NO_DUMMY,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(ILI_HOST, &devcfg, &s_spi),
                        TAG, "spi_bus_add_device");

    send_init();

    gpio_config_t bl = { .pin_bit_mask = 1ULL << s_pins.bl, .mode = GPIO_MODE_OUTPUT };
    ESP_RETURN_ON_ERROR(gpio_config(&bl), TAG, "gpio_config BL");
    gpio_set_level(s_pins.bl, s_pins.bl_active_low ? 0 : 1);   // turn on

    ESP_LOGI(TAG, "ILI9341 ready: %dx%d landscape (BL gpio=%d %s)",
             ILI_W, ILI_H, s_pins.bl,
             s_pins.bl_active_low ? "active-low" : "active-high");
    return ESP_OK;
}

esp_err_t ili9341_draw_rows(int y0, int y1, const uint16_t *pixels)
{
    if (y0 < 0 || y1 > ILI_H || y1 <= y0) return ESP_ERR_INVALID_ARG;

    set_window(0, y0, ILI_W - 1, y1 - 1);

    static uint16_t scratch[ILI_W];
    int rows = y1 - y0;
    dc_high(); cs_low();
    for (int y = 0; y < rows; y++) {
        const uint16_t *row = &pixels[y * ILI_W];
        for (int x = 0; x < ILI_W; x++) {
            uint16_t p = row[x];
            scratch[x] = (uint16_t)((p << 8) | (p >> 8));
        }
        spi_transaction_t t = { .length = ILI_W * 16, .tx_buffer = scratch };
        ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
    }
    cs_high();
    return ESP_OK;
}
