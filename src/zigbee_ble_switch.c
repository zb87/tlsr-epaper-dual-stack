#include "tl_common.h"
#include "zigbee_ble_switch.h"
#include "zb_common.h"
#include "stack/ble/ble.h"
#include "app_config.h"

app_dualModeInfo_t g_dualModeInfo = {
    .slot          = DUALMODE_SLOT_ZIGBEE,
    .bleState      = 0,
    .switch_to_ble = 0,
};

extern u8 g_zb_txPowerSet;
extern u8 g_ble_txPowerSet;
extern u8 zb_get_channel(void);

_attribute_ram_code_ void switch_to_zb_context(void) {
    unsigned char r = irq_disable();
    CURRENT_SLOT_SET(DUALMODE_SLOT_ZIGBEE);
    pm_select_internal_32k_rc();
    ZB_RADIO_RX_DISABLE;
    backup_ble_rf_context();
    restore_zb_rf_context();
    ZB_RADIO_TX_POWER_SET(g_zb_txPowerSet);
    u8 ch = zb_get_channel();
    if (ch >= 11 && ch <= 26) {
        ZB_TRANSCEIVER_SET_CHANNEL(ch);
    }
    rf_setTrxState(RF_STATE_RX);
    irq_restore(r);
}

_attribute_ram_code_ void switch_to_ble_context(void) {
    unsigned char r = irq_disable();
    ZB_RADIO_TX_DISABLE;
    ZB_RADIO_RX_DISABLE;
    restore_ble_rf_context();
    ZB_RADIO_TX_POWER_SET(g_ble_txPowerSet);
    ZB_RADIO_RX_ENABLE;
    CURRENT_SLOT_SET(DUALMODE_SLOT_BLE);
    irq_restore(r);
}
