#ifndef _BTHOME_BEACON_H_
#define _BTHOME_BEACON_H_

#include <stdint.h>
#include "app_config.h"

#define ADV_BTHOME_UUID16     0xFCD2
#define BtHomeID_Info         0x40 // BTHome v2 Unencrypted

// BTHome v2 Object IDs
typedef enum {
    BtHomeID_PacketId    = 0x00, // uint8
    BtHomeID_battery     = 0x01, // uint8 (%)
    BtHomeID_temperature = 0x02, // int16 (0.01 °C)
    BtHomeID_voltage     = 0x0C, // uint16 (0.001 V = 1 mV)
    BtHomeID_button      = 0x3A, // uint8
    BtHomeID_count16     = 0x3D, // uint16
    BtHomeID_count32     = 0x3E, // uint32
    BtHomeID_duration    = 0x43, // uint32 (0.001 s)
    BtHomeID_slot        = 0x4E, // uint8 (active slot)
} BTHOME_OBJ_ID_e;

// BTHome v2 Advertising Header
typedef struct __attribute__((packed)) {
    uint8_t  size; // Length of AD structure
    uint8_t  uid;  // 0x16: Service Data 16-bit UUID
    uint16_t UUID; // 0xFCD2: BTHome Service UUID
    uint8_t  info; // 0x40 (v2 unencrypted)
    uint8_t  p_id; // 0x00: Packet ID object ID
    uint8_t  pid;  // Packet sequence counter
} adv_bthome_head_t;

// Telemetry payload for Hanshow E31HA E-Paper
typedef struct __attribute__((packed)) {
    uint8_t  b_id;          // BtHomeID_battery (0x01)
    uint8_t  battery_level; // Battery percentage (0..100)
    uint8_t  t_id;          // BtHomeID_temperature (0x02)
    int16_t  temperature;   // Temperature in 0.01 °C
    uint8_t  v_id;          // BtHomeID_voltage (0x0C)
    uint16_t voltage;       // Battery voltage in mV
} adv_bthome_epaper_data_t;

typedef struct __attribute__((packed)) {
    uint8_t                  flag[3];
    adv_bthome_head_t        head;
    adv_bthome_epaper_data_t data;
    uint8_t                  name[13]; // [0x0C, GAP_ADTYPE_LOCAL_NAME_COMPLETE, 'T', 'L', 'S', 'R', '-', X, X, X, X, X, X]
} adv_bthome_beacon_t;

// Power statistics payload for BTHome v2 (30 bytes total)
typedef struct __attribute__((packed)) {
    uint8_t  cnt_id;      // BtHomeID_count32 (0x3E)
    uint32_t wakeup_count;// total wake-up count
    uint8_t  dur_id;      // BtHomeID_duration (0x43)
    uint32_t wakeup_dur;  // duration in ms (0.001s unit)
    uint8_t  tx_id;       // BtHomeID_duration (0x43)
    uint32_t tx_dur;      // duration in ms
    uint8_t  rx_id;       // BtHomeID_duration (0x43)
    uint32_t rx_dur;      // duration in ms
} adv_bthome_power_data_t;

typedef struct __attribute__((packed)) {
    uint8_t                 flag[3];
    adv_bthome_head_t       head;
    adv_bthome_power_data_t data;
} adv_bthome_power_beacon_t;

typedef struct {
    uint32_t send_count;
    uint16_t adv_restore_count;
    union {
        adv_bthome_beacon_t       data;
        adv_bthome_power_beacon_t pwr_data;
    };
} adv_buf_t;

extern adv_buf_t adv_buf;

void bthome_data_beacon(void);
void bthome_power_beacon(void);
int app_advertise_prepare_handler(void *p);

#endif // _BTHOME_BEACON_H_
