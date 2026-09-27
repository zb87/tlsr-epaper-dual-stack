#ifndef _POWER_TRACKER_H_
#define _POWER_TRACKER_H_

#include <stdint.h>
#include "tl_common.h"

enum {
    POWER_RF_OFF = 0,
    POWER_RF_TX  = 1,
    POWER_RF_RX  = 2,
};

typedef struct __attribute__((packed)) {
    uint32_t wakeup_count;
    uint32_t wakeup_duration_ms;
    uint32_t tx_duration_ms;
    uint32_t rx_duration_ms;
} power_stats_t;

void power_tracker_init(bool isRetention);
void power_tracker_on_wake(void);
void power_tracker_on_sleep(void);
void power_tracker_rf_notify(uint8_t rf_state);
void power_tracker_ble_rf_cb(int type);
void power_tracker_get_stats(power_stats_t *out);
uint32_t power_tracker_get_wakeup_count(void);

#endif // _POWER_TRACKER_H_
