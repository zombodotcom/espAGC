#pragma once
// Pin map mirrored from third_party/T-Dongle-C5/include/pin_config.h.
// Kept in our tree so the firmware never depends on the upstream Arduino headers.

#define BOARD_NAME "T-Dongle-C5"

// Shared SPI bus (LCD + SD)
#define BOARD_SPI_MOSI   2
#define BOARD_SPI_MISO   7
#define BOARD_SPI_SCK    6

// LCD: ST7735, 80x160 (portrait native)
#define BOARD_LCD_HRES   80
#define BOARD_LCD_VRES   160
#define BOARD_LCD_MOSI   BOARD_SPI_MOSI
#define BOARD_LCD_SCK    BOARD_SPI_SCK
#define BOARD_LCD_BL     0
#define BOARD_LCD_RST    1
#define BOARD_LCD_DC     3
#define BOARD_LCD_CS     10

// APA102 RGB status LED
#define BOARD_LED_CLK    4
#define BOARD_LED_DAT    5

// SD slot (unused in v1; SPI bus shared with LCD)
#define BOARD_SD_CMD     BOARD_SPI_MOSI
#define BOARD_SD_DAT0    BOARD_SPI_MISO
#define BOARD_SD_CLK     BOARD_SPI_SCK
#define BOARD_SD_CS      23

// UART0
#define BOARD_UART0_TX   11
#define BOARD_UART0_RX   12

// Boot button (pulled high; pressed = low)
#define BOARD_BUTTON_BOOT 28

#ifdef __cplusplus
extern "C" {
#endif

void board_init(void);

#ifdef __cplusplus
}
#endif
