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

_attribute_ram_code_ void switch_to_zb_context(void) {
    unsigned char r = irq_disable();
    ZB_RADIO_RX_DISABLE;
    backup_ble_rf_context();
    restore_zb_rf_context();
    ZB_RADIO_TX_POWER_SET(g_zb_txPowerSet);
    ZB_RADIO_RX_ENABLE;
    CURRENT_SLOT_SET(DUALMODE_SLOT_ZIGBEE);
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
