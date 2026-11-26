#include "u_printf.h"
#include <string.h>
#include "tl_common.h"
#include "app_config.h"
#include "flash_eep.h"
#include "battery.h"
#include "epd.h"
#include "epd_slots.h"
#include "ext_flash.h"
#include "OneBitDisplay.h"
#include "font16.h"
#if EPD_IS_750
#include "font30.h"
#endif

extern uint8_t mac_public[6];
extern const uint8_t ucMirror[256];
extern void flash_unlock(void);
extern void flash_read(u32 addr, u32 len, u8 *buf);

_attribute_custom_bss_ static OBDISP obd;
_attribute_custom_bss_ uint8_t shared_scratch_ram[SHARED_SCRATCH_RAM_SIZE];

#if EPD_IS_750
_attribute_custom_bss_ static uint16_t s_info_vbat_mv;
_attribute_custom_bss_ static uint8_t  s_info_temp;
_attribute_custom_bss_ static int8_t   s_info_band_loaded;
_attribute_custom_bss_ static uint32_t s_info_last_idx;
_attribute_custom_bss_ static int16_t  s_info_last_y;
_attribute_custom_bss_ static uint8_t  s_info_last_c;

// 8 User Slots for Endor 7.5" 800x480 panels (128 KB each: 2x 64 KB blocks in external GD25Q16EQ)
// Starts at 0x010000 (64 KB aligned) so address 0 remains the invalid-slot sentinel.
static const uint32_t flash_slots[EPD_USER_SLOT_COUNT] = {
    0x010000, // Slot 1 (0x010000 - 0x02FFFF)
    0x030000, // Slot 2 (0x030000 - 0x04FFFF)
    0x050000, // Slot 3 (0x050000 - 0x06FFFF)
    0x070000, // Slot 4 (0x070000 - 0x08FFFF)
    0x090000, // Slot 5 (0x090000 - 0x0AFFFF)
    0x0B0000, // Slot 6 (0x0B0000 - 0x0CFFFF)
    0x0D0000, // Slot 7 (0x0D0000 - 0x0EFFFF)
    0x0F0000  // Slot 8 (0x0F0000 - 0x10FFFF)
};
#elif EPD_IS_420
// 4 User Slots for 4.2" 400x300 panels (8 KB each: 2 sectors of 4 KB, within 0x78000 - 0x7FFFF)
static const uint32_t flash_slots[EPD_USER_SLOT_COUNT] = {
    0x78000, // Slot 1 (0x78000 - 0x79FFF)
    0x7A000, // Slot 2 (0x7A000 - 0x7BFFF)
    0x7C000, // Slot 3 (0x7C000 - 0x7DFFF)
    0x7E000  // Slot 4 (0x7E000 - 0x7FFFF)
};
#else
// 8 User Slots for M3Na (4 KB each: 1 sector of 4 KB, within 0x78000 - 0x7FFFF)
static const uint32_t flash_slots[EPD_USER_SLOT_COUNT] = {
    0x78000, // Slot 1
    0x79000, // Slot 2
    0x7A000, // Slot 3
    0x7B000, // Slot 4
    0x7C000, // Slot 5
    0x7D000, // Slot 6
    0x7E000, // Slot 7
    0x7F000  // Slot 8
};
#endif

void epd_slot_storage_read(uint32_t addr, uint32_t len, uint8_t *buf) {
#if EXT_FLASH_ENABLED
    ext_flash_read(addr, len, buf);
#else
    flash_read(addr, len, buf);
#endif
}

uint32_t epd_get_slot_flash_address(uint8_t slot_idx) {
    if (slot_idx < EPD_USER_SLOT_START || slot_idx >= (EPD_USER_SLOT_START + EPD_USER_SLOT_COUNT)) {
        return 0;
    }
    return flash_slots[slot_idx - EPD_USER_SLOT_START];
}

bool epd_get_slot_header(uint8_t slot_idx, epd_slot_header_t *header) {
    uint32_t addr = epd_get_slot_flash_address(slot_idx);
    if (addr == 0 || header == NULL) return false;
    epd_slot_storage_read(addr, sizeof(epd_slot_header_t), (uint8_t *)header);
    return (header->magic == EPD_COMP_MAGIC);
}

#if EPD_IS_750
static void epd_render_info_band_750(uint8_t band) {
    int y0 = (int)band * EPD_INFO_BAND_HEIGHT;
    int y1 = y0 + EPD_INFO_BAND_HEIGHT;

    obdCreateVirtualDisplay(&obd, EPD_WIDTH, EPD_INFO_BAND_HEIGHT, epd_render_buffer);
    obdFill(&obd, 0, 0); // White background (0 in OBD)

    char buf[64];
    const GFXfont *font = &Special_Elite_Regular_30;
    const char *style_names[] = { "Standard", "B&W std", "B&W inv", "R&W std", "R&W inv" };
    const char *s_name = (settings.render_style < STYLE_COUNT) ? style_names[settings.render_style] : "Custom";

    // Draw horizontal divider lines (2 px thick) if they fall in this band
    if (66 >= y0 && 66 < y1) obdDrawLine(&obd, 0, 66 - y0, EPD_WIDTH - 1, 66 - y0, 1, 0);
    if (67 >= y0 && 67 < y1) obdDrawLine(&obd, 0, 67 - y0, EPD_WIDTH - 1, 67 - y0, 1, 0);
    if (388 >= y0 && 388 < y1) obdDrawLine(&obd, 0, 388 - y0, EPD_WIDTH - 1, 388 - y0, 1, 0);
    if (389 >= y0 && 389 < y1) obdDrawLine(&obd, 0, 389 - y0, EPD_WIDTH - 1, 389 - y0, 1, 0);

    // Draw text lines whose bounding box overlaps [y0, y1)
    if (48 >= y0 - 8 && 48 < y1 + 32) {
        sprintf(buf, "%s DUAL-STACK", DEV_NAME);
        obdWriteStringCustom(&obd, (GFXfont *)font, 24, 48 - y0, buf, 1);
    }
    if (110 >= y0 - 8 && 110 < y1 + 32) {
        sprintf(buf, "Mode:    %s", (settings.active_mode == DEVICE_MODE_ZIGBEE) ? "ZIGBEE 3.0" : "BLE");
        obdWriteStringCustom(&obd, (GFXfont *)font, 32, 110 - y0, buf, 1);
    }
    if (162 >= y0 - 8 && 162 < y1 + 32) {
        sprintf(buf, "Style:   %s", s_name);
        obdWriteStringCustom(&obd, (GFXfont *)font, 32, 162 - y0, buf, 1);
    }
    if (214 >= y0 - 8 && 214 < y1 + 32) {
        sprintf(buf, "MAC:     %02X:%02X:%02X:%02X:%02X:%02X",
                mac_public[5], mac_public[4], mac_public[3],
                mac_public[2], mac_public[1], mac_public[0]);
        obdWriteStringCustom(&obd, (GFXfont *)font, 32, 214 - y0, buf, 1);
    }
    if (276 >= y0 - 8 && 276 < y1 + 32) {
        sprintf(buf, "Battery: %u.%02uV  |  Temp: %d'C",
                s_info_vbat_mv / 1000, (s_info_vbat_mv % 1000) / 10, s_info_temp);
        obdWriteStringCustom(&obd, (GFXfont *)font, 32, 276 - y0, buf, 1);
    }
    if (328 >= y0 - 8 && 328 < y1 + 32) {
        sprintf(buf, "Display: 7.5\" BWR (800x480)");
        obdWriteStringCustom(&obd, (GFXfont *)font, 32, 328 - y0, buf, 1);
    }
    if (436 >= y0 - 8 && 436 < y1 + 32) {
        sprintf(buf, "Active Slot: %u  |  Refreshes: %u", settings.active_slot, settings.screen_refresh_count);
        obdWriteStringCustom(&obd, (GFXfont *)font, 24, 436 - y0, buf, 1);
    }

    s_info_band_loaded = (int8_t)band;
}
#endif

void epd_render_info_slot(void) {
    uint16_t battery_mv = get_battery_mv();
    uint8_t temp = epd_read_temp();

#if EPD_IS_750
    s_info_vbat_mv = battery_mv;
    s_info_temp = temp;
    s_info_band_loaded = -1;
    s_info_last_idx = 0xFFFFFFFFUL;
    s_info_last_y = 0;
    s_info_last_c = 0;
#elif EPD_IS_420
    obdCreateVirtualDisplay(&obd, EPD_WIDTH, EPD_BUFFER_HEIGHT, epd_render_buffer);
    obdFill(&obd, 0, 0); // White background (0 in OBD)

    char buf[64];

    // -------------------------------------------------------------------------
    // 4.2" Display Layout (400 x 300)
    // -------------------------------------------------------------------------
    sprintf(buf, "%s DUAL-STACK", DEV_NAME);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 12, 22, buf, 1);
    obdDrawLine(&obd, 0, 30, 399, 30, 1, 0);

    if (settings.active_mode == DEVICE_MODE_ZIGBEE) {
        sprintf(buf, "Mode:    ZIGBEE 3.0");
    } else {
        sprintf(buf, "Mode:    BLE");
    }
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 16, 58, buf, 1);

    const char *style_names[] = { "Standard", "B&W std", "B&W inv", "R&W std", "R&W inv" };
    const char *s_name = (settings.render_style < STYLE_COUNT) ? style_names[settings.render_style] : "Custom";
    sprintf(buf, "Style:   %s", s_name);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 16, 88, buf, 1);

    sprintf(buf, "MAC:     %02X:%02X:%02X:%02X:%02X:%02X",
            mac_public[5], mac_public[4], mac_public[3],
            mac_public[2], mac_public[1], mac_public[0]);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 16, 118, buf, 1);

    sprintf(buf, "Battery: %u.%02uV  |  Temp: %d'C",
            battery_mv / 1000, (battery_mv % 1000) / 10, temp);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 16, 148, buf, 1);

    sprintf(buf, "Display: 4.2\" BWR (400x300)");
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 16, 178, buf, 1);

    obdDrawLine(&obd, 0, 238, 399, 238, 1, 0);

    sprintf(buf, "Active Slot: %u  |  Total Refreshes: %u", settings.active_slot, settings.screen_refresh_count);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 16, 268, buf, 1);

#else
    obdCreateVirtualDisplay(&obd, EPD_WIDTH, EPD_BUFFER_HEIGHT, epd_render_buffer);
    obdFill(&obd, 0, 0); // White background (0 in OBD)

    char buf[64];

    // -------------------------------------------------------------------------
    // 2.13" Display Layout (250 x 122)
    // -------------------------------------------------------------------------
    obdDrawLine(&obd, 0, 20, 249, 20, 1, 0);
    obdDrawLine(&obd, 0, 104, 249, 104, 1, 0);

    sprintf(buf, "TLSR8258 DUAL-STACK ESL");
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 6, 16, buf, 1);

    if (settings.active_mode == DEVICE_MODE_ZIGBEE) {
        sprintf(buf, "Mode:  ZIGBEE 3.0");
    } else {
        sprintf(buf, "Mode:  BLE");
    }
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 8, 36, buf, 1);

    const char *style_names[] = { "Standard", "B&W std", "B&W inv", "R&W std", "R&W inv" };
    const char *s_name = (settings.render_style < STYLE_COUNT) ? style_names[settings.render_style] : "Custom";
    sprintf(buf, "Style: %s", s_name);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 8, 53, buf, 1);

    sprintf(buf, "MAC:   %02X:%02X:%02X:%02X:%02X:%02X",
            mac_public[5], mac_public[4], mac_public[3],
            mac_public[2], mac_public[1], mac_public[0]);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 8, 70, buf, 1);

    sprintf(buf, "Bat:   %u.%02uV | %d'C",
            battery_mv / 1000, (battery_mv % 1000) / 10, temp);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 8, 87, buf, 1);

    sprintf(buf, "Slot: %u", settings.active_slot);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 8, 120, buf, 1);
#endif
}

uint8_t epd_get_info_pixel_byte(uint32_t byte_idx) {
    if (byte_idx >= EPD_PLANE_SIZE) return 0xFF;

#if EPD_IS_750
    // 800x480 horizontal rows, 100 bytes per line, banded in 4x 800x120 strips
    int y;
    int c;
    if (byte_idx == s_info_last_idx + 1) {
        c = s_info_last_c + 1;
        y = s_info_last_y;
        if (c >= EPD_LINE_BYTES) {
            c = 0;
            y++;
        }
    } else if (byte_idx == 0) {
        y = 0;
        c = 0;
    } else {
        y = (int)(byte_idx / EPD_LINE_BYTES);
        c = (int)(byte_idx % EPD_LINE_BYTES);
    }
    s_info_last_idx = byte_idx;
    s_info_last_y = (int16_t)y;
    s_info_last_c = (uint8_t)c;

    uint8_t band;
    int y0;
    if (y >= 360) {
        band = 3;
        y0 = 360;
    } else if (y >= 240) {
        band = 2;
        y0 = 240;
    } else if (y >= 120) {
        band = 1;
        y0 = 120;
    } else {
        band = 0;
        y0 = 0;
    }
    if ((int8_t)band != s_info_band_loaded) {
        epd_render_info_band_750(band);
    }
    int local_y = y - y0;
    int page_idx = local_y >> 3;
    int bit_idx = local_y & 7;
    const uint8_t *src_col = &epd_render_buffer[page_idx * EPD_WIDTH + (c << 3)];
    uint8_t epd_byte = 0xFF;
    uint8_t mask = (uint8_t)(1U << bit_idx);
    for (int bit = 0; bit < 8; bit++) {
        if (src_col[bit] & mask) {
            epd_byte &= (uint8_t)~(0x80U >> bit); // 0 = Black in canonical EPD plane
        }
    }
    return epd_byte;
#elif EPD_IS_420
    // 400x300 horizontal rows, 50 bytes per line
    int y = byte_idx / 50;
    int c = byte_idx % 50;
    int page_idx = y >> 3;
    int bit_idx = y & 7;
    const uint8_t *src_page = &epd_render_buffer[page_idx * 400];
    uint8_t epd_byte = 0xFF;
    for (int bit = 0; bit < 8; bit++) {
        int x = (c << 3) + bit;
        if ((src_page[x] >> bit_idx) & 1) {
            epd_byte &= ~(0x80 >> bit); // 0 = Black in EPD
        }
    }
    return epd_byte;
#else
    // 250x122 rotated layout: pDst[y + x * (height / 8)] = ~ucMirror[s[width - 1 - x]];
    int y = byte_idx % 16;
    int x = byte_idx / 16;
    const uint8_t *s = &epd_render_buffer[y * 250];
    return ~ucMirror[s[250 - 1 - x]];
#endif
}

void epd_prepare_slot_upload(uint8_t slot_idx, uint8_t plane) {
    (void)plane;
    uint32_t addr = epd_get_slot_flash_address(slot_idx);
    if (addr == 0) return;

#if EXT_FLASH_ENABLED
    ext_flash_wake();
    ext_flash_erase_block64k(addr);
    ext_flash_erase_block64k(addr + 0x10000);
#else
    flash_unlock();
    flash_erase(addr);
#if EPD_IS_420
    flash_erase(addr + 0x1000); // 8 KB slot: erase second 4 KB sector
#endif
#endif
}

bool epd_write_slot_chunk(uint8_t slot_idx, uint8_t plane, uint32_t offset, const uint8_t *data, uint16_t len) {
    (void)plane;
    uint32_t addr = epd_get_slot_flash_address(slot_idx);
    if (addr == 0 || data == NULL || len == 0) return false;
    if (offset >= EPD_SLOT_SIZE) return false;
    if (offset + len > EPD_SLOT_SIZE) {
        len = (uint16_t)(EPD_SLOT_SIZE - offset);
    }

#if EXT_FLASH_ENABLED
    ext_flash_write(addr + offset, len, data);
#else
    flash_write(addr + offset, len, (uint8_t *)data);
#endif
    return true;
}

void epd_commit_slot_upload(uint8_t slot_idx, uint8_t has_red_plane, uint8_t auto_display) {
    (void)has_red_plane;
    if (slot_idx < EPD_USER_SLOT_START || slot_idx >= (EPD_USER_SLOT_START + EPD_USER_SLOT_COUNT)) return;

    settings.active_slot = slot_idx;
    flash_eep_save();

    if (auto_display) {
        epd_display_slot(slot_idx, settings.render_style);
    }
}

void epd_erase_slot(uint8_t slot_idx) {
    uint32_t addr = epd_get_slot_flash_address(slot_idx);
    if (addr == 0) return;

#if EXT_FLASH_ENABLED
    ext_flash_wake();
    ext_flash_erase_block64k(addr);
    ext_flash_erase_block64k(addr + 0x10000);
    ext_flash_sleep();
#else
    flash_unlock();
    flash_erase(addr);
#if EPD_IS_420
    flash_erase(addr + 0x1000);
#endif
#endif
}

uint8_t epd_read_slot_chunk(uint8_t slot_idx, uint8_t plane, uint32_t offset, uint8_t *dst, uint8_t len) {
    if (slot_idx >= EPD_SLOT_COUNT || dst == NULL || len == 0) {
        return 0;
    }

    if (slot_idx == EPD_SLOT_INFO) {
        if (offset >= EPD_PLANE_SIZE) return 0;
        if (offset + len > EPD_PLANE_SIZE) {
            len = (uint8_t)(EPD_PLANE_SIZE - offset);
        }
        if (plane == 0) {
            if (offset == 0) {
                epd_render_info_slot();
            }
            for (uint16_t i = 0; i < len; i++) {
                dst[i] = epd_get_info_pixel_byte(offset + i);
            }
        } else {
            memset(dst, 0x00, len);
        }
        return len;
    } else if (slot_idx == EPD_SLOT_BLANK) {
        if (offset >= EPD_PLANE_SIZE) return 0;
        if (offset + len > EPD_PLANE_SIZE) {
            len = (uint8_t)(EPD_PLANE_SIZE - offset);
        }
        memset(dst, (plane == 0) ? 0xFF : 0x00, len);
        return len;
    } else {
        uint32_t addr = epd_get_slot_flash_address(slot_idx);
        if (addr == 0 || offset >= EPD_SLOT_SIZE) return 0;
        if (offset + len > EPD_SLOT_SIZE) {
            len = (uint8_t)(EPD_SLOT_SIZE - offset);
        }
        epd_slot_storage_read(addr + offset, len, dst);
        return len;
    }
}
