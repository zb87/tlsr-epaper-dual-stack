#include "power_tracker.h"
#include "app_config.h"
#include "debug_uart.h"
#include <string.h>

extern int device_in_connection_state; // From ble_app.c

typedef struct {
    power_stats_t stats;
    uint32_t wake_start_tick;
    uint32_t awake_ticks_rem;
    uint32_t rf_last_tick;
    uint32_t tx_ticks_rem;
    uint32_t rx_ticks_rem;
    uint8_t  rf_state;
    uint8_t  wake_active;
} power_tracker_state_t;

// Resides in Retention SRAM (.bss)
static power_tracker_state_t s_pwr;
#if DEBUG
static uint32_t s_last_logged_wake;
#endif

void power_tracker_init(bool isRetention) {
    if (!isRetention) {
        memset(&s_pwr, 0, sizeof(s_pwr));
#if DEBUG
        s_last_logged_wake = 0;
#endif
    }
}

_attribute_ram_code_
void power_tracker_on_wake(void) {
    if (!s_pwr.wake_active) {
        s_pwr.stats.wakeup_count++;
        s_pwr.wake_start_tick = clock_time();
        s_pwr.wake_active = 1;
    }
}

_attribute_ram_code_
void power_tracker_rf_notify(uint8_t state) {
    if (state == s_pwr.rf_state) {
        return;
    }
    uint32_t now = clock_time();
    uint32_t dt = now - s_pwr.rf_last_tick;
    s_pwr.rf_last_tick = now;

    if (s_pwr.rf_state == POWER_RF_TX) {
        s_pwr.tx_ticks_rem += dt;
        s_pwr.stats.tx_duration_ms += s_pwr.tx_ticks_rem / CLOCK_SYS_CLOCK_1MS;
        s_pwr.tx_ticks_rem %= CLOCK_SYS_CLOCK_1MS;
    } else if (s_pwr.rf_state == POWER_RF_RX) {
        s_pwr.rx_ticks_rem += dt;
        s_pwr.stats.rx_duration_ms += s_pwr.rx_ticks_rem / CLOCK_SYS_CLOCK_1MS;
        s_pwr.rx_ticks_rem %= CLOCK_SYS_CLOCK_1MS;
    }

    s_pwr.rf_state = state;
}

_attribute_ram_code_
void power_tracker_ble_rf_cb(int type) {
    if (type == 1) {        // PA_TYPE_TX_ON
        power_tracker_rf_notify(POWER_RF_TX);
    } else if (type == 2) { // PA_TYPE_RX_ON
        power_tracker_rf_notify(POWER_RF_RX);
    } else {                // PA_TYPE_OFF
        power_tracker_rf_notify(POWER_RF_OFF);
    }
}

void power_tracker_on_sleep(void) {
    // 1. Close out any active RF interval
    if (s_pwr.rf_state != POWER_RF_OFF) {
        power_tracker_rf_notify(POWER_RF_OFF);
    }

    // 2. Accumulate awake time for the current wake cycle
    if (s_pwr.wake_active) {
        uint32_t now = clock_time();
        uint32_t dt = now - s_pwr.wake_start_tick;
        s_pwr.awake_ticks_rem += dt;
        s_pwr.stats.wakeup_duration_ms += s_pwr.awake_ticks_rem / CLOCK_SYS_CLOCK_1MS;
        s_pwr.awake_ticks_rem %= CLOCK_SYS_CLOCK_1MS;
        s_pwr.wake_active = 0;
    }

    // 3. Serial debug logging on every 10th wake (e.g. 10, 20, 30...)
    // Format: "Wake up #20, 15ms, TX 3ms, RX 4ms"
    // Suppressed when BLE is connected
#if DEBUG
    if ((s_pwr.stats.wakeup_count > 0) &&
        (s_pwr.stats.wakeup_count % 10 == 0) &&
        (s_last_logged_wake != s_pwr.stats.wakeup_count) &&
        !device_in_connection_state) {
        s_last_logged_wake = s_pwr.stats.wakeup_count;
        DEBUG_PRINT("Wake up #%u, %ums, TX %ums, RX %ums\r\n",
                    (unsigned int)s_pwr.stats.wakeup_count,
                    (unsigned int)s_pwr.stats.wakeup_duration_ms,
                    (unsigned int)s_pwr.stats.tx_duration_ms,
                    (unsigned int)s_pwr.stats.rx_duration_ms);
        debug_uart_flush();
    }
#endif
}

void power_tracker_get_stats(power_stats_t *out) {
    if (!out) return;
    uint32_t now = clock_time();
    power_stats_t cur = s_pwr.stats;

    if (s_pwr.wake_active) {
        uint32_t dt = now - s_pwr.wake_start_tick;
        uint32_t rem = s_pwr.awake_ticks_rem + dt;
        cur.wakeup_duration_ms += rem / CLOCK_SYS_CLOCK_1MS;
    }
    if (s_pwr.rf_state == POWER_RF_TX) {
        uint32_t dt = now - s_pwr.rf_last_tick;
        uint32_t rem = s_pwr.tx_ticks_rem + dt;
        cur.tx_duration_ms += rem / CLOCK_SYS_CLOCK_1MS;
    } else if (s_pwr.rf_state == POWER_RF_RX) {
        uint32_t dt = now - s_pwr.rf_last_tick;
        uint32_t rem = s_pwr.rx_ticks_rem + dt;
        cur.rx_duration_ms += rem / CLOCK_SYS_CLOCK_1MS;
    }

    *out = cur;
}

uint32_t power_tracker_get_wakeup_count(void) {
    return s_pwr.stats.wakeup_count;
}
