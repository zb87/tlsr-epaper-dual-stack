#include "u_printf.h"
#include <string.h>
#include "tl_common.h"
#include "app_config.h"
#include "flash_eep.h"
#include "battery.h"
#include "epd.h"
#include "epd_slots.h"
#include "OneBitDisplay.h"
#include "font16.h"

extern uint8_t mac_public[6];
extern const uint8_t ucMirror[256];
extern void flash_unlock(void);

_attribute_custom_bss_ static OBDISP obd;
_attribute_custom_bss_ uint8_t shared_scratch_ram[SHARED_SCRATCH_RAM_SIZE];

#if (BOARD == BOARD_HANSHOW_E31PA)
// 4 User Slots for XL3Na (8 KB each: 2 sectors of 4 KB, within 0x78000 - 0x7FFFF)
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

uint32_t epd_get_slot_flash_address(uint8_t slot_idx) {
    if (slot_idx < EPD_USER_SLOT_START || slot_idx >= (EPD_USER_SLOT_START + EPD_USER_SLOT_COUNT)) {
        return 0;
    }
    return flash_slots[slot_idx - EPD_USER_SLOT_START];
}

bool epd_get_slot_header(uint8_t slot_idx, epd_slot_header_t *header) {
    uint32_t addr = epd_get_slot_flash_address(slot_idx);
    if (addr == 0 || header == NULL) return false;
    flash_read(addr, sizeof(epd_slot_header_t), (uint8_t *)header);
    return (header->magic == EPD_COMP_MAGIC);
}

void epd_render_info_slot(void) {
    uint16_t battery_mv = get_battery_mv();
    uint8_t temp = epd_read_temp();

    obdCreateVirtualDisplay(&obd, EPD_WIDTH, EPD_BUFFER_HEIGHT, epd_render_buffer);
    obdFill(&obd, 0, 0); // White background (0 in OBD)

    char buf[64];

#if (BOARD == BOARD_HANSHOW_E31PA)
    // -------------------------------------------------------------------------
    // 4.2" Display Layout (400 x 300)
    // -------------------------------------------------------------------------
    sprintf(buf, "TLSR-XL3Na-E31PA DUAL-STACK");
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

uint8_t epd_get_info_pixel_byte(uint16_t byte_idx) {
    if (byte_idx >= EPD_PLANE_SIZE) return 0xFF;

#if (BOARD == BOARD_HANSHOW_E31PA)
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

    flash_unlock();
    flash_erase(addr);
#if (BOARD == BOARD_HANSHOW_E31PA)
    flash_erase(addr + 0x1000); // 8 KB slot: erase second 4 KB sector
#endif
}

bool epd_write_slot_chunk(uint8_t slot_idx, uint8_t plane, uint16_t offset, const uint8_t *data, uint16_t len) {
    (void)plane;
    uint32_t addr = epd_get_slot_flash_address(slot_idx);
    if (addr == 0 || data == NULL || len == 0) return false;
    if (offset >= EPD_SLOT_SIZE) return false;
    if (offset + len > EPD_SLOT_SIZE) {
        len = EPD_SLOT_SIZE - offset;
    }

    flash_write(addr + offset, len, (uint8_t *)data);
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

    flash_unlock();
    flash_erase(addr);
#if (BOARD == BOARD_HANSHOW_E31PA)
    flash_erase(addr + 0x1000);
#endif
}

uint8_t epd_read_slot_chunk(uint8_t slot_idx, uint8_t plane, uint16_t offset, uint8_t *dst, uint8_t len) {
    if (slot_idx >= EPD_SLOT_COUNT || dst == NULL || len == 0) {
        return 0;
    }

    if (slot_idx == EPD_SLOT_INFO) {
        if (offset >= EPD_PLANE_SIZE) return 0;
        if (offset + len > EPD_PLANE_SIZE) {
            len = EPD_PLANE_SIZE - offset;
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
            len = EPD_PLANE_SIZE - offset;
        }
        memset(dst, (plane == 0) ? 0xFF : 0x00, len);
        return len;
    } else {
        uint32_t addr = epd_get_slot_flash_address(slot_idx);
        if (addr == 0 || offset >= EPD_SLOT_SIZE) return 0;
        if (offset + len > EPD_SLOT_SIZE) {
            len = EPD_SLOT_SIZE - offset;
        }
        flash_read(addr + offset, len, dst);
        return len;
    }
}
