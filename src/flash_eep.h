#ifndef _FLASH_EEP_H_
#define _FLASH_EEP_H_

#include <stdint.h>
#include <stdbool.h>
#include "app_config.h"

#define EEP_MAGIC 0xD005

typedef struct __attribute__((packed)) {
    uint16_t magic;                // EEP_MAGIC (0xD005)
    uint16_t sequence;             // Monotonic write sequence counter
    uint8_t  active_mode;          // 1 = Zigbee (default), 2 = BLE
    uint8_t  active_slot;          // 0 = Info, 1 = Blank, 2..7 = User Images
    uint8_t  render_style;         // 0 = Standard, 1 = B&W, 2 = B&W inv, 3 = R&W, 4 = R&W inv
    uint8_t  reserved;             // Reserved (was refresh_mode)
    uint16_t screen_refresh_count; // Total screen refreshes
    uint32_t fw_version;           // Firmware build version (FILE_VERSION)
    uint16_t crc;                  // CRC16 over preceding 14 bytes
} device_settings_t;

extern RAM device_settings_t settings;

extern u8 mcuBootAddr;
u8 mcuBootAddrGet(void);

void flash_eep_init(void);
bool flash_eep_load(void);
void flash_eep_save(void);
void flash_eep_set_default(void);

#endif // _FLASH_EEP_H_
