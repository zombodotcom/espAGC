// boards/board_cyd_2432s028/include/board_pins.h
#pragma once
//
// Pin map for ESP32-2432S028C ("Cheap Yellow Display", 2.8" capacitive variant).
// Reference: third_party/CYD-reference (witnessmenow/ESP32-Cheap-Yellow-Display).
//
// The "C" capacitive variant moves the LCD backlight from GPIO21 (where the
// "R" resistive variant has it) to GPIO27, which frees GPIO21 for the
// CST820 touch INT line.

#define BOARD_NAME "CYD-2432S028C"

// LCD: ILI9341, 240x320 native; rotated to 320x240 landscape via MADCTL
#define BOARD_LCD_HRES   320
#define BOARD_LCD_VRES   240
#define BOARD_LCD_SCK    14
#define BOARD_LCD_MOSI   13
#define BOARD_LCD_MISO   12      // shared with onboard SD slot
#define BOARD_LCD_CS     15
#define BOARD_LCD_DC     2
#define BOARD_LCD_RST    -1      // tied to EN — no GPIO control
#define BOARD_LCD_BL     27      // active-high backlight

// Touch: CST820 capacitive (I2C, 7-bit addr 0x15)
#define BOARD_TOUCH_SDA  33
#define BOARD_TOUCH_SCL  32
#define BOARD_TOUCH_RST  25
#define BOARD_TOUCH_INT  21

// RGB LED — 3 separate active-low GPIOs
#define BOARD_LED_R       4
#define BOARD_LED_G      16
#define BOARD_LED_B      17

// SD card (unused).
#define BOARD_SD_CS       5

// Boot button (pulled high; pressed = low)
#define BOARD_BUTTON_BOOT 0

#ifdef __cplusplus
extern "C" {
#endif

#include "display_panel_iface.h"
#include "panel_touch_iface.h"
#include "led_status_iface.h"

void board_init(void);
const display_panel_iface_t *board_get_panel(void);
const panel_touch_iface_t   *board_get_touch(void);
const led_status_iface_t    *board_get_led(void);

#ifdef __cplusplus
}
#endif
