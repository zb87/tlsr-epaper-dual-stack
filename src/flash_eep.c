#include <string.h>
#include "tl_common.h"
#include "app_config.h"
#include "flash_eep.h"

extern void flash_unlock(void);

#define SECTOR0_ADDR            FMEMORY_EEP_BASE_ADDR            // 0x78000
#define SECTOR1_ADDR            (FMEMORY_EEP_BASE_ADDR + 0x1000) // 0x79000
#define SECTOR_SIZE             4096
#define ENTRY_SIZE              sizeof(device_settings_t)        // 16 bytes
#define ENTRIES_PER_SECTOR      (SECTOR_SIZE / ENTRY_SIZE)       // 256 entries

RAM device_settings_t settings;

RAM static uint32_t active_sector_addr = 0;
RAM static uint16_t next_entry_idx = 0;
RAM static uint16_t current_seq = 0;

static uint16_t calc_crc16(const uint8_t *data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xA001;
            else crc >>= 1;
        }
    }
    return crc;
}

static bool is_blank_entry(const uint8_t *buf, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        if (buf[i] != 0xFF) return false;
    }
    return true;
}

void flash_eep_set_default(void) {
    memset(&settings, 0, sizeof(settings));
    settings.magic = EEP_MAGIC;
    settings.sequence = 0;
    settings.active_mode = DEVICE_MODE_ZIGBEE; // Zigbee default out-of-the-box
    settings.active_slot = EPD_SLOT_INFO;      // Slot 0 (Info page)
    settings.render_style = STYLE_STANDARD;    // Standard rendering
    settings.reserved = 0;
    settings.screen_refresh_count = 0;
    settings.fw_version = FILE_VERSION;
    settings.crc = calc_crc16((const uint8_t *)&settings, sizeof(device_settings_t) - sizeof(uint16_t));
}

bool flash_eep_load(void) {
    device_settings_t temp;
    device_settings_t best_entry;
    uint32_t best_sector = 0;
    int16_t best_idx = -1;
    uint16_t best_seq = 0;
    bool found_any = false;

    uint32_t sectors[2] = { SECTOR0_ADDR, SECTOR1_ADDR };

    for (int s = 0; s < 2; s++) {
        uint32_t sec_addr = sectors[s];
        for (int i = 0; i < ENTRIES_PER_SECTOR; i++) {
            flash_read(sec_addr + (i * ENTRY_SIZE), ENTRY_SIZE, (uint8_t *)&temp);
            if (temp.magic == EEP_MAGIC) {
                uint16_t expected_crc = calc_crc16((const uint8_t *)&temp, sizeof(device_settings_t) - sizeof(uint16_t));
                if (temp.crc == expected_crc) {
                    if (!found_any) {
                        found_any = true;
                        best_entry = temp;
                        best_sector = sec_addr;
                        best_idx = i;
                        best_seq = temp.sequence;
                    } else {
                        int16_t diff = (int16_t)(temp.sequence - best_seq);
                        if (diff > 0) {
                            best_entry = temp;
                            best_sector = sec_addr;
                            best_idx = i;
                            best_seq = temp.sequence;
                        }
                    }
                }
            }
        }
    }

    if (!found_any) {
        flash_eep_set_default();
        flash_unlock();
        flash_erase(SECTOR0_ADDR);
        flash_erase(SECTOR1_ADDR);
        extern u8 nv_resetAll(void);
        nv_resetAll();
        settings.sequence = 1;
        settings.crc = calc_crc16((const uint8_t *)&settings, sizeof(device_settings_t) - sizeof(uint16_t));
        flash_write(SECTOR0_ADDR, sizeof(device_settings_t), (uint8_t *)&settings);
        active_sector_addr = SECTOR0_ADDR;
        next_entry_idx = 1;
        current_seq = 1;
        return false; // Cold boot / uninitialized
    }

    memcpy(&settings, &best_entry, sizeof(settings));
    if (settings.active_mode != DEVICE_MODE_ZIGBEE && settings.active_mode != DEVICE_MODE_BLE) {
        settings.active_mode = DEVICE_MODE_ZIGBEE;
    }
    if (settings.active_slot >= EPD_SLOT_COUNT) {
        settings.active_slot = EPD_SLOT_INFO;
    }
    if (settings.render_style >= STYLE_COUNT) {
        settings.render_style = STYLE_BW_STANDARD;
    }

    active_sector_addr = best_sector;
    current_seq = best_entry.sequence;

    // Search for next unwritten entry slot in active sector
    next_entry_idx = ENTRIES_PER_SECTOR;
    for (int i = best_idx + 1; i < ENTRIES_PER_SECTOR; i++) {
        flash_read(active_sector_addr + (i * ENTRY_SIZE), ENTRY_SIZE, (uint8_t *)&temp);
        if (is_blank_entry((const uint8_t *)&temp, ENTRY_SIZE)) {
            next_entry_idx = i;
            break;
        }
    }

    if (settings.fw_version != FILE_VERSION) {
        settings.fw_version = FILE_VERSION;
        flash_eep_save();
    }

    return true;
}

void flash_eep_save(void) {
    if (active_sector_addr == 0) {
        if (!flash_eep_load()) {
            return;
        }
    }

    if (next_entry_idx >= ENTRIES_PER_SECTOR) {
        // Active sector full: rotate to other sector and erase it
        active_sector_addr = (active_sector_addr == SECTOR0_ADDR) ? SECTOR1_ADDR : SECTOR0_ADDR;
        flash_unlock();
        flash_erase(active_sector_addr);
        next_entry_idx = 0;
    }

    settings.magic = EEP_MAGIC;
    settings.sequence = ++current_seq;
    settings.crc = calc_crc16((const uint8_t *)&settings, sizeof(device_settings_t) - sizeof(uint16_t));

    flash_unlock();
    flash_write(active_sector_addr + (next_entry_idx * ENTRY_SIZE), sizeof(device_settings_t), (uint8_t *)&settings);
    next_entry_idx++;
}

#if !ZCL_OTA_SUPPORT
u8 mcuBootAddr = 0;

u8 mcuBootAddrGet(void) {
    u8 flashInfo = 0;
    flash_read(8, 1, &flashInfo);
    return (flashInfo == 0x4B) ? 0 : 1;
}
#endif

void flash_eep_init(void) {
    mcuBootAddr = mcuBootAddrGet();
    flash_eep_load();
}
