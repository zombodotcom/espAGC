/*
 * st7735_panel.c — native ESP-IDF C port of LilyGO's upstream
 * Adafruit_ST7735 driver for the T-Dongle-C5's onboard 0.96" 80x160 panel.
 *
 * Primary source (vendor-supplied, lives in this repo as a submodule):
 *   third_party/T-Dongle-C5/lib/lcd_st7735/st7735.{h,cpp}
 *   (https://github.com/Xinyuan-LilyGO/T-Dongle-C5)
 *
 * Init sequence, timing, MADCTL/COLMOD/INVON, and address-window offsets
 * (x_gap=1, y_gap=26 in landscape) are byte-for-byte identical to the
 * upstream's `sendInitCommands()` and `setAddrWindow()`. The C++ class
 * was rewritten in plain C against esp-idf's spi_master/gpio drivers so
 * we don't have to pull in arduino-esp32 + Adafruit_GFX.
 *
 * We don't use esp_lcd's panel abstraction or the maintained waveshare
 * managed component because that component:
 *   - sends NORON / DISPON with a stray {0x00} data byte (those
 *     commands take ZERO arguments per the ST7735 datasheet),
 *   - sends INVOFF instead of INVON during init (this panel needs
 *     INVON inside the init stream, not as a post-init toggle),
 *   - uses a 10ms / 10ms reset pulse (this panel needs Adafruit's
 *     100 / 100 / 120 ms sequence to come out of reset reliably).
 *
 * Pinout (LilyGO examples/Factory/pin_config.h):
 *   MOSI=2, SCK=6, CS=10, DC=3, RST=1, BL=0, SD_CS=23.
 *   SPI clock 40 MHz. PIN_LCD_BL is ACTIVE LOW.
 *
 * Panel quirks:
 *   - BGR order, MADCTL bit 0x08 always set
 *   - Inverted display: INVON sent inside init
 *   - Address-window offsets: COLSTART=26, ROWSTART=1
 *   - Landscape (160x80) MADCTL = 0xA0 | 0x08 = 0xA8 (rotation 3)
 */
#include <string.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"

#include "st7735_panel.h"

#define ST7735_HOST           SPI2_HOST
#define PIN_LCD_MOSI          2
#define PIN_LCD_SCK           6
#define PIN_LCD_CS            10
#define PIN_LCD_DC            3
#define PIN_LCD_RST           1
#define PIN_LCD_BL            0
#define PIN_SD_CS             23
/* T-Dongle-C5 has a 1-pixel APA102 RGB LED on GPIO 4/5. Left
 * undriven, it latches noise from neighboring traces and blinks
 * distractingly. We send one all-off frame at boot to silence it. */
#define PIN_LED_CI            4
#define PIN_LED_DI            5
#define LCD_SPI_HZ            (40 * 1000 * 1000)

/* ST7735 commands. */
#define ST7735_SWRESET   0x01
#define ST7735_SLPOUT    0x11
#define ST7735_NORON     0x13
#define ST7735_INVOFF    0x20
#define ST7735_INVON     0x21
#define ST7735_DISPOFF   0x28
#define ST7735_DISPON    0x29
#define ST7735_CASET     0x2A
#define ST7735_RASET     0x2B
#define ST7735_RAMWR     0x2C
#define ST7735_MADCTL    0x36
#define ST7735_COLMOD    0x3A
#define ST7735_FRMCTR1   0xB1
#define ST7735_FRMCTR2   0xB2
#define ST7735_FRMCTR3   0xB3
#define ST7735_INVCTR    0xB4
#define ST7735_PWCTR1    0xC0
#define ST7735_PWCTR2    0xC1
#define ST7735_PWCTR3    0xC2
#define ST7735_PWCTR4    0xC3
#define ST7735_PWCTR5    0xC4
#define ST7735_VMCTR1    0xC5
#define ST7735_GMCTRP1   0xE0
#define ST7735_GMCTRN1   0xE1

/* Marker byte in init_cmds[] meaning "next byte is a delay in ms,
 * not a data argument count". Same semantics as Adafruit's. */
#define DELAY            0x80

#define COLSTART         26
#define ROWSTART          1
#define MADCTL_LANDSCAPE 0xA8

static const char *TAG = "st7735";

static spi_device_handle_t s_spi;

static void cs_low(void)  { gpio_set_level(PIN_LCD_CS, 0); }
static void cs_high(void) { gpio_set_level(PIN_LCD_CS, 1); }
static void dc_low(void)  { gpio_set_level(PIN_LCD_DC, 0); }
static void dc_high(void) { gpio_set_level(PIN_LCD_DC, 1); }

static void spi_tx(const uint8_t *bytes, size_t len)
{
    if (len == 0) return;
    spi_transaction_t t = { .length = len * 8, .tx_buffer = bytes };
    cs_low();
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
    cs_high();
}
static void write_cmd  (uint8_t cmd)                  { dc_low();  spi_tx(&cmd, 1); }
static void write_data (const uint8_t *data, size_t n){ dc_high(); spi_tx(data, n); }

static void send_init_commands(void)
{
    static const uint8_t init_cmds[] = {
        ST7735_SWRESET,  DELAY, 150,
        ST7735_SLPOUT,   DELAY, 255,
        ST7735_FRMCTR1,  3, 0x01, 0x2C, 0x2D,
        ST7735_FRMCTR2,  3, 0x01, 0x2C, 0x2D,
        ST7735_FRMCTR3,  6, 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D,
        ST7735_INVCTR,   1, 0x07,
        ST7735_PWCTR1,   3, 0xA2, 0x02, 0x84,
        ST7735_PWCTR2,   1, 0xC5,
        ST7735_PWCTR3,   2, 0x0A, 0x00,
        ST7735_PWCTR4,   2, 0x8A, 0x2A,
        ST7735_PWCTR5,   2, 0x8A, 0xEE,
        ST7735_VMCTR1,   1, 0x0E,
        ST7735_INVON,    0,
        ST7735_COLMOD,   1, 0x05,
        ST7735_MADCTL,   1, MADCTL_LANDSCAPE,
        ST7735_GMCTRP1, 16, 0x02, 0x1c, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2d,
                            0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10,
        ST7735_GMCTRN1, 16, 0x03, 0x1d, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
                            0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10,
        ST7735_NORON,    DELAY, 10,
        ST7735_DISPON,   DELAY, 100,
        0x00
    };

    const uint8_t *p = init_cmds;
    while (p[0] != 0x00) {
        uint8_t cmd = p[0];
        write_cmd(cmd);
        if (p[1] == DELAY) {
            vTaskDelay(pdMS_TO_TICKS(p[2]));
            p += 3;
        } else {
            uint8_t n = p[1];
            if (n > 0) write_data(&p[2], n);
            p += 2 + n;
        }
    }
}

static void set_addr_window(uint16_t x0, uint16_t y0,
                            uint16_t x1, uint16_t y1)
{
    x0 += ROWSTART; x1 += ROWSTART;
    y0 += COLSTART; y1 += COLSTART;
    uint8_t caset[4] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
    write_cmd(ST7735_CASET); write_data(caset, 4);
    uint8_t raset[4] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };
    write_cmd(ST7735_RASET); write_data(raset, 4);
    write_cmd(ST7735_RAMWR);
}

static void deselect_sd_card(void)
{
    gpio_config_t cfg = { .pin_bit_mask = 1ULL << PIN_SD_CS, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&cfg);
    gpio_set_level(PIN_SD_CS, 1);
}

static void apa102_byte(uint8_t b)
{
    for (int i = 7; i >= 0; i--) {
        gpio_set_level(PIN_LED_DI, (b >> i) & 1);
        gpio_set_level(PIN_LED_CI, 1);
        gpio_set_level(PIN_LED_CI, 0);
    }
}
static void apa102_off_frame(void)
{
    apa102_byte(0); apa102_byte(0); apa102_byte(0); apa102_byte(0);
    apa102_byte(0xE0);
    apa102_byte(0); apa102_byte(0); apa102_byte(0);
    apa102_byte(0xFF);
    gpio_set_level(PIN_LED_CI, 0);
    gpio_set_level(PIN_LED_DI, 0);
}
static void silence_apa102_led(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_LED_CI) | (1ULL << PIN_LED_DI),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cfg);
    gpio_set_drive_capability(PIN_LED_CI, GPIO_DRIVE_CAP_3);
    gpio_set_drive_capability(PIN_LED_DI, GPIO_DRIVE_CAP_3);
    gpio_set_level(PIN_LED_CI, 0);
    gpio_set_level(PIN_LED_DI, 0);
    apa102_off_frame();
}

esp_err_t st7735_init(void)
{
    deselect_sd_card();
    silence_apa102_led();

    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << PIN_LCD_CS) | (1ULL << PIN_LCD_DC) | (1ULL << PIN_LCD_RST),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_cfg), TAG, "gpio_config CS/DC/RST");

    cs_high();
    gpio_set_level(PIN_LCD_RST, 1); vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PIN_LCD_RST, 0); vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PIN_LCD_RST, 1); vTaskDelay(pdMS_TO_TICKS(120));

    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_LCD_MOSI, .miso_io_num = -1, .sclk_io_num = PIN_LCD_SCK,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = DSKY_FB_W * 8 * (int)sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(ST7735_HOST, &buscfg, SPI_DMA_CH_AUTO),
                        TAG, "spi_bus_initialize");

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = LCD_SPI_HZ, .mode = 0, .spics_io_num = -1,
        .queue_size = 4, .flags = SPI_DEVICE_NO_DUMMY,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(ST7735_HOST, &devcfg, &s_spi),
                        TAG, "spi_bus_add_device");

    send_init_commands();

    static uint16_t black_row[DSKY_FB_W];
    memset(black_row, 0, sizeof black_row);
    set_addr_window(0, 0, DSKY_FB_W - 1, DSKY_FB_H - 1);
    dc_high(); cs_low();
    for (int y = 0; y < DSKY_FB_H; y++) {
        spi_transaction_t t = { .length = DSKY_FB_W * 16, .tx_buffer = black_row };
        ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
    }
    cs_high();

    gpio_config_t bl_cfg = { .pin_bit_mask = 1ULL << PIN_LCD_BL, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&bl_cfg);
    gpio_set_level(PIN_LCD_BL, 0);   /* active-low backlight ON */

    ESP_LOGI(TAG, "ST7735 ready: %dx%d landscape, BGR, MADCTL=0x%02x",
             DSKY_FB_W, DSKY_FB_H, MADCTL_LANDSCAPE);
    return ESP_OK;
}

esp_err_t st7735_draw_rows(int y0, int y1, const uint16_t *pixels)
{
    if (y0 < 0 || y1 > DSKY_FB_H || y1 <= y0) return ESP_ERR_INVALID_ARG;

    set_addr_window(0, y0, DSKY_FB_W - 1, y1 - 1);

    static uint16_t scratch[DSKY_FB_W];
    int rows = y1 - y0;
    dc_high(); cs_low();
    for (int y = 0; y < rows; y++) {
        const uint16_t *row = &pixels[y * DSKY_FB_W];
        for (int x = 0; x < DSKY_FB_W; x++) {
            uint16_t p = row[x];
            scratch[x] = (uint16_t)((p << 8) | (p >> 8));
        }
        spi_transaction_t t = { .length = DSKY_FB_W * 16, .tx_buffer = scratch };
        ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
    }
    cs_high();

    apa102_off_frame();
    return ESP_OK;
}
