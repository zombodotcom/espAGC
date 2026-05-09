// apa102_status.c — bit-banged APA102 driver for the single onboard RGB LED.
// Two-wire protocol (clock + data); 32-bit start frame, one 32-bit LED frame,
// 32-bit end frame. Brightness limited to keep the dongle cool.

#include "led_status.h"
#include "board_pins.h"

#include "driver/gpio.h"
#include "esp_rom_sys.h"

static inline void clk_pulse(void)
{
    gpio_set_level(BOARD_LED_CLK, 1);
    esp_rom_delay_us(1);
    gpio_set_level(BOARD_LED_CLK, 0);
}

static void send_byte(uint8_t b)
{
    for (int i = 7; i >= 0; i--) {
        gpio_set_level(BOARD_LED_DAT, (b >> i) & 1);
        clk_pulse();
    }
}

static void apa102_write(uint8_t r, uint8_t g, uint8_t b)
{
    // Start frame
    for (int i = 0; i < 4; i++) send_byte(0x00);
    // 0xE0 | brightness (0..31). Keep low to limit current.
    send_byte(0xE0 | 0x04);
    send_byte(b);
    send_byte(g);
    send_byte(r);
    // End frame (32 bits high).
    for (int i = 0; i < 4; i++) send_byte(0xFF);
}

void led_status_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BOARD_LED_CLK) | (1ULL << BOARD_LED_DAT),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cfg);
    apa102_write(0, 0, 0);
}

void led_status_update(const dsky_state_t *state)
{
    if (state->prog_alarm || state->restart) {
        apa102_write(64, 0, 0);
    } else if (state->uplink_acty) {
        apa102_write(0, 0, 32);
    } else if (state->comp_acty) {
        apa102_write(0, 32, 0);
    } else {
        apa102_write(0, 0, 0);
    }
}
