#include "tl_common.h"
#include "app_config.h"
#include "stack/ble/ble.h"
#include "battery.h"
#include "epd.h"
#include "flash_eep.h"
#include "bthome_beacon.h"
#include "power_tracker.h"

extern u8 mac_public[6];

static inline char int_to_hex(u8 num) {
    num &= 0x0f;
    return (num < 10) ? ('0' + num) : ('A' + num - 10);
}

adv_buf_t adv_buf;

__attribute__((optimize("-Os")))
void bthome_data_beacon(void) {
    adv_bthome_beacon_t *p = &adv_buf.data;
    uint16_t vbat = get_battery_mv();
    uint8_t bat_pct = get_battery_level(vbat);
    int16_t temp_centi = (int16_t)epd_read_temp() * 100; // 0.01 °C units

    p->flag[0] = 0x02;
    p->flag[1] = GAP_ADTYPE_FLAGS;
    p->flag[2] = 0x04 | GAP_ADTYPE_LE_GENERAL_DISCOVERABLE_MODE_BIT;

    p->head.uid = GAP_ADTYPE_SERVICE_DATA_UUID_16BIT; // 0x16
    p->head.UUID = ADV_BTHOME_UUID16;
    p->head.info = BtHomeID_Info;
    p->head.p_id = BtHomeID_PacketId;
    p->head.pid = (uint8_t)adv_buf.send_count;

    p->data.b_id = BtHomeID_battery;
    p->data.battery_level = bat_pct;
    p->data.t_id = BtHomeID_temperature;
    p->data.temperature = temp_centi;
    p->data.v_id = BtHomeID_voltage;
    p->data.voltage = vbat;

    p->head.size = sizeof(adv_bthome_head_t) - 1 + sizeof(adv_bthome_epaper_data_t);

    p->name[0] = 12; // 1 byte type + 11 bytes name
    p->name[1] = GAP_ADTYPE_LOCAL_NAME_COMPLETE; // 0x09
    p->name[2] = 'T';
    p->name[3] = 'L';
    p->name[4] = 'S';
    p->name[5] = 'R';
    p->name[6] = '-';
    p->name[7] = int_to_hex(mac_public[2] >> 4);
    p->name[8] = int_to_hex(mac_public[2] & 0x0f);
    p->name[9] = int_to_hex(mac_public[1] >> 4);
    p->name[10] = int_to_hex(mac_public[1] & 0x0f);
    p->name[11] = int_to_hex(mac_public[0] >> 4);
    p->name[12] = int_to_hex(mac_public[0] & 0x0f);
}

__attribute__((optimize("-Os")))
void bthome_power_beacon(void) {
    adv_bthome_power_beacon_t *p = &adv_buf.pwr_data;
    power_stats_t st;
    power_tracker_get_stats(&st);

    p->flag[0] = 0x02;
    p->flag[1] = GAP_ADTYPE_FLAGS;
    p->flag[2] = 0x04 | GAP_ADTYPE_LE_GENERAL_DISCOVERABLE_MODE_BIT;

    p->head.uid = GAP_ADTYPE_SERVICE_DATA_UUID_16BIT; // 0x16
    p->head.UUID = ADV_BTHOME_UUID16;
    p->head.info = BtHomeID_Info;
    p->head.p_id = BtHomeID_PacketId;
    p->head.pid = (uint8_t)adv_buf.send_count;

    p->data.cnt_id = BtHomeID_count32; // 0x3E
    p->data.wakeup_count = st.wakeup_count;
    p->data.dur_id = BtHomeID_duration; // 0x43 (0.001s unit)
    p->data.wakeup_dur = st.wakeup_duration_ms;
    p->data.tx_id = BtHomeID_duration;  // 0x43 (0.001s unit)
    p->data.tx_dur = st.tx_duration_ms;
    p->data.rx_id = BtHomeID_duration;  // 0x43 (0.001s unit)
    p->data.rx_dur = st.rx_duration_ms;

    p->head.size = sizeof(adv_bthome_head_t) - 1 + sizeof(adv_bthome_power_data_t);
}

_attribute_ram_code_
int app_advertise_prepare_handler(void *p) {
    (void)p;
    adv_buf.send_count++;
    if ((adv_buf.send_count & 1) == 0) {
        bthome_data_beacon();
        bls_ll_setAdvData((u8 *)&adv_buf.data, sizeof(adv_buf.data));
    } else {
        bthome_power_beacon();
        bls_ll_setAdvData((u8 *)&adv_buf.pwr_data, sizeof(adv_buf.pwr_data));
    }
    return 1;
}
