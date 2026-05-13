// app_main.c — espAGC top-level glue.
//
//  +---------------------+   +---------------------+
//  | agc_task            |   | ui_task             |
//  | runs agc_core_step  |   | snapshots state and |
//  | engine writes go    |   | renders the 320x240 |
//  | through io_callbacks|   | DSKY framebuffer    |
//  +----------+----------+   +----------+----------+
//             │                          ▲
//             ▼                          │
//        channel_router  ◄───── snapshot read
//             ▲
//             │
//   +---------+----------+----------+
//   | touch   | wifi_ap  | (button) |
//   +--------------------+----------+

#include "agc_core.h"
#include "apollo_rom.h"
#include "board_pins.h"
#include "channel_router.h"
#include "dsky_keys.h"
#include "display_hal.h"
#include "dsky_input.h"
#include "display_panel_iface.h"
#include "led_status_iface.h"
#include "peripheral_stub.h"
#include "touch_input.h"
#include "dsky_layout.h"

#include "yaagc_socket.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <ctype.h>

// APP_CPU_NUM / PRO_CPU_NUM are defined by ESP-IDF's soc.h via FreeRTOS,
// but provide fallback macros in case a future SDK rearranges headers.
#ifndef APP_CPU_NUM
#define APP_CPU_NUM 1
#endif
#ifndef PRO_CPU_NUM
#define PRO_CPU_NUM 0
#endif

static const char *TAG = "app";

extern const display_panel_iface_t *board_get_panel(void);
extern const panel_touch_iface_t   *board_get_touch(void);
extern const led_status_iface_t    *board_get_led(void);

static apollo_rom_id_t pick_rom(void)
{
    // Boot button held during reset → CSM (Comanche055). Default → LM.
    return gpio_get_level(BOARD_BUTTON_BOOT) == 0
        ? APOLLO_ROM_COMANCHE055
        : APOLLO_ROM_LUMINARY099;
}

// AGC engine task, pinned to APP_CPU (core 1). The ESP32's
// interpreted-C agc_engine isn't fast enough to hit AGC's nominal
// 1.024 MHz simulated rate — measurement shows ~200-300 kHz on
// 160 MHz ESP32. vTaskDelayUntil's drift-corrected pacing degenerates
// to a tight loop when each batch overruns its target wall-clock,
// starving IDLE1 → task watchdog trips.
//
// Pragma: ALWAYS vTaskDelay(1) between batches so IDLE1 runs every
// FreeRTOS tick (10ms default). The engine runs as fast as the CPU
// allows during the active portion of each tick. Effective rate is
// ~2000 cycles / 10ms = ~200 kHz simulated, ~5x slower than nominal
// AGC clock. Luminary doesn't care about absolute wall-clock speed,
// only about relative cycle ordering and interrupt-period ratios.
static void agc_task(void *arg)
{
    (void)arg;
    const int batch_cycles = 2000;   // ~few ms wall-clock on ESP32
    for (;;) {
        agc_core_step(batch_cycles);
        vTaskDelay(1);   // always yield ≥1 tick to IDLE1 + lower-pri tasks
    }
}

static void ui_task(void *arg)
{
    (void)arg;
    dsky_state_t state;
    uint64_t last_gen = (uint64_t)-1;
    for (;;) {
        uint64_t gen = channel_router_snapshot(&state);
        if (gen != last_gen) {
            display_hal_update(&state);
            last_gen = gen;
        }
        vTaskDelay(pdMS_TO_TICKS(33));   // ~30 Hz
    }
}

// Map an ASCII character to a DSKY keycode. Returns -1 for chars that
// don't correspond to any key. Mirrors the WSL capture script's keymap
// (tests/host/capture_with_dumps.sh) so the same one-character commands
// drive both the WSL reference and our build.
//
//   V=verb N=noun +/- E=ENTR C=CLR P=PRO R=RSET K=KEY REL
//   0..9 = digit keys (with 0 mapped to DSKY_KEY_0 = 16)
static int serial_ascii_to_keycode(int c)
{
    c = toupper(c);
    switch (c) {
        case 'V': return DSKY_KEY_VERB;
        case 'N': return DSKY_KEY_NOUN;
        case '+': return DSKY_KEY_PLUS;
        case '-': return DSKY_KEY_MINUS;
        case 'E': return DSKY_KEY_ENTR;
        case 'C': return DSKY_KEY_CLR;
        case 'P': return DSKY_KEY_PRO;
        case 'R': return DSKY_KEY_RSET;
        case 'K': return DSKY_KEY_KEYREL;
        case '0': return DSKY_KEY_0;
        case '1': case '2': case '3': case '4': case '5':
        case '6': case '7': case '8': case '9':
            return c - '0';
        default:  return -1;
    }
}

// Serial input task — drives keypresses from the UART0 console.
//
// Purpose: under QEMU there's no emulated WiFi PHY or touch controller,
// so neither dsky_input (web DSKY) nor touch_input (CST820/XPT2046) can
// inject keystrokes. UART0 is the one input QEMU does expose — `idf.py
// qemu monitor` connects stdin/stdout to UART0. Type "V37E" into the
// monitor to dispatch verb 37 enter, exactly like pressing it on the
// hardware DSKY. Works on real hardware too (useful for headless boot
// scripts or anyone with a USB-serial console), so we keep it always-on.
//
// Whitespace (space, newline, tab) is ignored as a separator. Unknown
// characters are silently dropped — typos can't crash the engine.
static void serial_input_task(void *arg)
{
    (void)arg;
    // The console UART is configured by ESP-IDF for the log pipe but the
    // *driver* isn't installed by default — uart_read_bytes silently
    // returns 0 without it. Install a minimal RX-only driver on UART0.
    // (TX is already used by the log output; we don't replace that.)
    const uart_port_t port = UART_NUM_0;
    if (uart_is_driver_installed(port) == false) {
        // 256-byte RX buffer is plenty for human typing speed.
        esp_err_t err = uart_driver_install(port, 256, 0, 0, NULL, 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "uart_driver_install(UART0) failed: %d — serial input disabled", err);
            vTaskDelete(NULL);
        }
    }
    ESP_LOGI(TAG, "serial_input: ready (UART0). type V/N/+/-/E/C/P/R/K/0-9 to drive DSKY");
    for (;;) {
        uint8_t ch;
        int n = uart_read_bytes(port, &ch, 1, pdMS_TO_TICKS(100));
        if (n <= 0) continue;
        int code = serial_ascii_to_keycode(ch);
        if (code < 0) continue;
        ESP_LOGI(TAG, "serial: '%c' -> dsky_key=%d", ch, code);
        channel_router_post_key(code);
    }
}

void app_main(void)
{
    board_init();

    const led_status_iface_t *led = board_get_led();
    if (led) {
        led->init();
        led->set_rgb(0xFF, 0x80, 0x00);   // amber while booting
    }

    channel_router_init();
    display_hal_init();   // brings up the ST7789 panel + 320x240 layout

    apollo_rom_id_t rom_id = pick_rom();
    size_t rom_size = 0;
    const uint8_t *rom = apollo_rom_get(rom_id, &rom_size);
    ESP_LOGI(TAG, "loading ROM %s (%u bytes)", apollo_rom_name(rom_id),
             (unsigned)rom_size);

    int rc = agc_core_init(rom, rom_size);
    if (rc != 0) {
        ESP_LOGE(TAG, "agc_core_init failed: %d", rc);
        return;
    }

    // Tell agc_engine which rope it's running. Defaults to 0 (LM); set
    // to 1 for CM (Comanche055). Gates ch013 RHC handling, ch166/167/170
    // optics, and the absence of ch0163 lamp writes — without this the
    // CM rope boots into a state where PINBALL never renders, which is
    // why holding BOOT at reset showed a black screen.
    extern int CmOrLm;
    CmOrLm = (rom_id == APOLLO_ROM_COMANCHE055) ? 1 : 0;
    ESP_LOGI(TAG, "engine mode: %s (CmOrLm=%d)",
             CmOrLm ? "CM" : "LM", CmOrLm);

    // Initialize the peripheral simulator. Future stages of this will
    // continuously drive ch030/031/032/033 and push CDU counter pulses
    // (mimicking what LM_Simulator does on Pi/Linux). For now it's a
    // hook for that work.
    peripheral_stub_init();

    dsky_input_config_t in_cfg = {
#ifdef CONFIG_ESPAGC_DISABLE_WIFI
        .enable_wifi_ap = false,     // QEMU build: no WiFi PHY available
#else
        .enable_wifi_ap = true,
#endif
        .wifi_ssid = "espAGC",
        .wifi_password = "",     // open network
    };
    dsky_input_start(&in_cfg);

    const panel_touch_iface_t *touch = board_get_touch();
    if (touch) {
        const display_panel_iface_t *panel = board_get_panel();
        const dsky_layout_t *layout = dsky_layout_for(panel->width, panel->height);
        if (layout && layout->hit_test) {
            touch_input_start(touch, layout->hit_test);
        } else {
            ESP_LOGW(TAG, "touch present but layout has no hit_test — skipping");
        }
    }

    // Canonical SocketAPI listener. MUST run before agc_task starts —
    // the engine's first ChannelOutput iterates s_clients[], and if the
    // array is still zero-initialised at that point every slot looks
    // like fd=0 to send(), triggering an immediate "client 0 dropped
    // (send errno=9)" and tearing down the synthetic local client
    // (slot 0). Bring the listener up BEFORE creating the engine task
    // so its slot table is in the right shape (synthetic at 0, -1
    // elsewhere) by the time the first engine cycle runs.
    int sock_rc = yaagc_socket_init(CONFIG_AGC_YAAGC_NODE_PORT);
    if (sock_rc != 0) {
        ESP_LOGE(TAG, "yaagc_socket_init failed (port %d)",
                 CONFIG_AGC_YAAGC_NODE_PORT);
    }

    // ESP32 dual-core: pin AGC engine to APP_CPU (core 1) at high
    // priority so its real-time pacing isn't perturbed by WiFi/touch
    // bursts on PRO_CPU (core 0). UI/snapshot work runs on core 0
    // alongside the radio stack. This mirrors WSL's setup (yaAGC as
    // one process, peripheral simulators as another process) — except
    // the "cores" replace the process boundary.
    xTaskCreatePinnedToCore(agc_task, "agc", 6144, NULL, 10, NULL, APP_CPU_NUM);
    xTaskCreatePinnedToCore(ui_task,  "ui",  6144, NULL,  5, NULL, PRO_CPU_NUM);
    // serial_input is small + blocks on UART read, leave unpinned. Useful
    // primarily under QEMU (no WiFi/touch), but harmless on hardware too.
    xTaskCreate(serial_input_task, "serial_in", 3072, NULL, 4, NULL);

    ESP_LOGI(TAG, "espAGC running");
    if (led) led->set_rgb(0x00, 0x40, 0x00);   // dim green = healthy
}
