#include "tl_common.h"
#include "zb_common.h"
#include "stack/ble/ble.h"
#include "zigbee_ble_switch.h"
#include "app_config.h"
#include "flash_eep.h"
#include "battery.h"
#include "led.h"
#include "epd.h"
#include "epd_slots.h"
#include "mode_switch.h"
#include "nfc_fm11nc08.h"
#include "zigbee/zb_app.h"
#include "ble/ble_app.h"
#include "debug_uart.h"
#include "power_tracker.h"

extern startup_state_e drv_platform_init(void);
extern u8 mcuBootAddrGet(void);

int main(void) {
    startup_state_e state = drv_platform_init();
    bool isRetention = (state != SYSTEM_RETENTION_NONE);

    power_tracker_init(isRetention);
    power_tracker_on_wake();

    #if DEBUG
    reg_clk_en0 |= FLD_CLK0_UART_EN;
    #endif

    if (!isRetention) {
        reg_clk_en0 |= FLD_CLK0_SWIRE_EN;
        // Cold boot peripheral setup:
        flash_unlock(); // Unlock SPI flash write-protection for NVRAM & settings

        // Optimization 1: Conditional SWS debugger attach window
        // If valid configuration exists in flash (normal battery swap / restart), shorten window to 250 ms.
        // If unconfigured / factory blank flash, provide full 2000 ms attach window.
        bool has_valid_config = flash_eep_load();
        WaitMs(has_valid_config ? 250 : 2000);

        extern uint8_t mac_public[6];
        flash_read(CFG_ADR_MAC, 6, mac_public); // Pre-load 6-byte public MAC for EPD Slot 0 & NFC

        debug_uart_init();
        DEBUG_LOG("BOOT", "=== TLSR8258 Dual-Stack ESL Boot ===");
        DEBUG_LOG("BOOT", "Firmware: %08X", (unsigned int)FILE_VERSION);
        u8 bank = mcuBootAddrGet();
        DEBUG_LOG("BOOT", "Active Bank: %s (%s)", bank ? "Bank 1 [B1]" : "Bank 0 [B0]", bank ? "0x40000" : "0x00000");
        DEBUG_LOG("BOOT", "MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                  mac_public[5], mac_public[4], mac_public[3],
                  mac_public[2], mac_public[1], mac_public[0]);

        led_init();
        battery_init();

        DEBUG_LOG("BOOT", "Mode: %s | Slot: %u | Style: %u",
                  (settings.active_mode == DEVICE_MODE_BLE) ? "BLE 5.0" : "Zigbee 3.0",
                  settings.active_slot, settings.render_style);

        u8 prev_ota_err = analog_read(0x3a);
        if (prev_ota_err != 0) {
            u16 prev_ota_blk = analog_read(0x3b) | (analog_read(0x38) << 8);
            DEBUG_LOG("BOOT", "PREVIOUS OTA FAILED: Code %u at Block %u", prev_ota_err, prev_ota_blk);
            analog_write(0x3a, 0);
            analog_write(0x3b, 0);
            analog_write(0x38, 0);
        }

        epd_init();
        nfc_fm11nc08_init(true);

        // Render initial screen slot
        epd_display_slot(settings.active_slot, settings.render_style);
    } else {
        // Retention wakeup peripheral restore
#if DEBUG
        debug_uart_init();
#endif
        led_restore_retention();
        if (epd_update_state == 1) {
            // Restore EPD power gate and pins during active screen refresh in retention mode
            gpio_set_func(GPIO_EPD_PWR_ENABLE, AS_GPIO);
            gpio_set_output_en(GPIO_EPD_PWR_ENABLE, 1);
            gpio_set_input_en(GPIO_EPD_PWR_ENABLE, 0);
            gpio_write(GPIO_EPD_PWR_ENABLE, 0);
#if (BOARD == BOARD_HANSHOW_E31PA)
            gpio_setup_up_down_resistor(GPIO_EPD_PWR_ENABLE, PM_PIN_UP_DOWN_FLOAT);
#else
            gpio_setup_up_down_resistor(GPIO_EPD_PWR_ENABLE, PM_PIN_PULLDOWN_100K);
#endif

            gpio_set_func(GPIO_EPD_RESET, AS_GPIO);
            gpio_set_output_en(GPIO_EPD_RESET, 1);
            gpio_write(GPIO_EPD_RESET, 1);
            gpio_setup_up_down_resistor(GPIO_EPD_RESET, PM_PIN_PULLUP_1M);

            gpio_set_func(GPIO_EPD_CS, AS_GPIO);
            gpio_set_output_en(GPIO_EPD_CS, 1);
            gpio_write(GPIO_EPD_CS, 1);
            gpio_setup_up_down_resistor(GPIO_EPD_CS, PM_PIN_PULLUP_1M);

            gpio_set_func(GPIO_EPD_BUSY, AS_GPIO);
            gpio_set_output_en(GPIO_EPD_BUSY, 0);
            gpio_set_input_en(GPIO_EPD_BUSY, 1);
#if (BOARD == BOARD_HANSHOW_E31PA)
            gpio_setup_up_down_resistor(GPIO_EPD_BUSY, PM_PIN_UP_DOWN_FLOAT);
#else
            gpio_setup_up_down_resistor(GPIO_EPD_BUSY, PM_PIN_PULLUP_1M);
#endif
        }
        // Event-gated NFC: only initialize I2C / process NFC if RF field is detected
        if (!gpio_read(GPIO_NFC_IRQ)) {
            nfc_fm11nc08_init(false);
        }
    }

    if (settings.active_mode == DEVICE_MODE_BLE) {
        CURRENT_SLOT_SET(DUALMODE_SLOT_BLE);
        ble_radio_init();
        user_ble_init(isRetention);
        if (!isRetention) {
            led_blink(LED_BLUE, 100); // Blue LED blink confirms BLE mode boot
        }
        drv_enable_irq();

        while (1) {
            ble_task();
            if (!ota_is_working) {
                if (!gpio_read(GPIO_NFC_IRQ) || nfc_fm11nc08_is_active()) {
                    nfc_process_events();
                }
                epd_state_handler();
            }
            ble_pm_task();
        }
    } else {
        if (!isRetention) {
            DEBUG_LOG("MAIN", "Zigbee boot path: calling os_init(isRetention=%d)", (int)isRetention);
        }
        os_init(isRetention);
        CURRENT_SLOT_SET(DUALMODE_SLOT_ZIGBEE);
        if (!isRetention) {
            DEBUG_LOG("MAIN", "Calling user_zb_init(isRetention=%d)", (int)isRetention);
        }
        user_zb_init(isRetention);
        if (!isRetention) {
            led_blink(LED_GREEN, 100); // Green LED blink confirms Zigbee mode boot
            DEBUG_LOG("MAIN", "Zigbee ready, entering main loop");
        }
        drv_enable_irq();

        while (1) {
            zb_task();
            if (!gpio_read(GPIO_NFC_IRQ) || nfc_fm11nc08_is_active()) {
                nfc_process_events();
            }
            epd_state_handler();
            zb_pm_task();
        }
    }

    return 0;
}
