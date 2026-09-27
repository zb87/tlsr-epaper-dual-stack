#ifndef _EPD_SLOTS_H_
#define _EPD_SLOTS_H_

#include <stdint.h>
#include <stdbool.h>
#include "app_config.h"

#define EPD_COMP_MAGIC 0x5A42 // 'ZB'

typedef struct __attribute__((packed)) {
    uint16_t magic;         // 0x5A42 (EPD_COMP_MAGIC)
    uint8_t  version;       // 1
    uint8_t  flags;         // bit 0: has_red_plane (1 = BWR, 0 = BW only)
    uint16_t width;         // Image width (250 or 400)
    uint16_t height;        // Image height (122 or 300)
    uint16_t bw_comp_len;   // Byte length of compressed BW plane
    uint16_t red_comp_len;  // Byte length of compressed Red plane (0 if no red plane)
    uint16_t reserved;      // 0
    uint16_t checksum;      // 0
} epd_slot_header_t;

// Renders dynamic info screen (Slot 0) into epd_render_buffer
void epd_render_info_slot(void);

// Returns pixel byte for Info screen at byte offset 0..EPD_PLANE_SIZE-1
uint8_t epd_get_info_pixel_byte(uint16_t byte_idx);

// Get base flash address for a given user slot index (1..EPD_USER_SLOT_COUNT)
uint32_t epd_get_slot_flash_address(uint8_t slot_idx);

// Reads slot header from flash
bool epd_get_slot_header(uint8_t slot_idx, epd_slot_header_t *header);

// Slot operations
void epd_prepare_slot_upload(uint8_t slot_idx, uint8_t plane);
bool epd_write_slot_chunk(uint8_t slot_idx, uint8_t plane, uint16_t offset, const uint8_t *data, uint16_t len);
void epd_commit_slot_upload(uint8_t slot_idx, uint8_t has_red_plane, uint8_t auto_display);
void epd_erase_slot(uint8_t slot_idx);
uint8_t epd_read_slot_chunk(uint8_t slot_idx, uint8_t plane, uint16_t offset, uint8_t *dst, uint8_t len);

#endif // _EPD_SLOTS_H_
