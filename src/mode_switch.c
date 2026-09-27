#include "tl_common.h"
#include "app_config.h"
#include "flash_eep.h"
#include "led.h"
#include "epd.h"
#include "mode_switch.h"
#include "zb_api.h"
#include "zigbee/zb_app.h"
#include "debug_uart.h"

extern void start_reboot(void);

void mcu_soft_reboot(void) {
    debug_uart_flush();
    start_reboot();
}

void mode_switch_to(device_mode_t target_mode) {
    if (target_mode != DEVICE_MODE_ZIGBEE && target_mode != DEVICE_MODE_BLE) {
        return;
    }

    DEBUG_LOG("SYS", "Mode switch requested: target mode = %s", (target_mode == DEVICE_MODE_BLE) ? "BLE" : "Zigbee");

    if (settings.active_mode == target_mode) {
        // Already in target mode:
        if (target_mode == DEVICE_MODE_BLE) {
            led_blink(LED_BLUE, 200);
        } else {
            if (!zb_isDeviceJoinedNwk()) {
                zb_start_pairing();
            } else {
                led_blink(LED_GREEN, 200);
            }
        }
        return;
    }

    // 1. Update settings
    settings.active_mode = target_mode;
    settings.active_slot = EPD_SLOT_INFO; // Switch to Slot 0 (Info Screen)
    flash_eep_save();

    // 2. Visual LED indication:
    // Blue for BLE mode, Green for Zigbee mode
    if (target_mode == DEVICE_MODE_BLE) {
        led_blink(LED_BLUE, 300);
    } else {
        led_blink(LED_GREEN, 300);
    }

    // 3. If screen refresh is currently active, cleanly sleep the panel before reset
    if (epd_is_busy()) {
        epd_set_sleep();
    }

    WaitMs(100);

    // 4. Clean reboot into target stack (EPD display will refresh automatically on boot)
    mcu_soft_reboot();
}

extern void zb_resetDevice2FN(void);
extern void zb_deviceFactoryNewSet(bool new);
extern u8 nv_resetAll(void);
extern void flash_unlock(void);

void mode_switch_zigbee_reset(void) {
    DEBUG_LOG("ZIGBEE", "Zigbee factory reset requested via NFC/command; clearing NVRAM and rebooting...");

    // 1. Visual LED indication: 5 rapid Green flashes to indicate Zigbee Network Reset
    for (int i = 0; i < 5; i++) {
        led_blink(LED_GREEN, 60);
        WaitMs(60);
    }

    // 2. Perform Factory Reset on Zigbee stack & clear NVRAM
    flash_unlock();
    if (settings.active_mode == DEVICE_MODE_ZIGBEE) {
        zb_resetDevice2FN();
        zb_deviceFactoryNewSet(true);
    }
    nv_resetAll();

    // 3. Set operating mode to Zigbee and default to Info slot
    settings.active_mode = DEVICE_MODE_ZIGBEE;
    settings.active_slot = EPD_SLOT_INFO;
    flash_eep_save();

    // 4. If screen refresh is currently active, cleanly sleep the panel before reset
    if (epd_is_busy()) {
        epd_set_sleep();
    }

    WaitMs(100);

    // 5. Clean reboot into Zigbee pairing mode (EPD display will refresh automatically on boot)
    mcu_soft_reboot();
}
