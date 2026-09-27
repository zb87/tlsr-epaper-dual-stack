#include <string.h>
#include "tl_common.h"
#include "app_config.h"
#include "epd.h"
#include "epd_spi.h"
#include "epd_prototype.h"
#include "debug_uart.h"
#include "flash_eep.h"

extern void flash_unlock(void);
extern u32 _ictag_start_;
extern void ble_prototype_pump(void);
extern u8 mcuBootAddrGet(void);

static void proto_sleep_ms(uint32_t ms) {
    while (ms > 0) {
        uint32_t step = (ms > 2) ? 2 : ms;
        sleep_us(step * 1000);
        ble_prototype_pump();
        ms -= step;
    }
}

static void proto_power_on(void) {
    EPD_POWER_ON();
}

static void proto_power_off(void) {
    EPD_POWER_OFF();
}

bool epd_prototype_commit(uint32_t flash_addr, const uint8_t *code_buf, uint16_t len) {
    if (!code_buf || len == 0 || len > PROTOTYPE_CODE_MAX_SIZE) return false;
    if (flash_addr == 0) flash_addr = PROTOTYPE_CODE_ADDR;

    // Bounds and sector alignment check
    if ((flash_addr & 0xFFF) != 0 ||
        flash_addr < PROTOTYPE_CODE_ADDR ||
        (flash_addr + len) > (PROTOTYPE_CODE_ADDR + PROTOTYPE_CODE_MAX_SIZE)) {
        DEBUG_LOG("PROTO", "Commit address 0x%05X len %u out of bounds!", flash_addr, len);
        return false;
    }

    DEBUG_LOG("PROTO", "Erasing & programming %u B @ 0x%05X...", len, flash_addr);
    flash_unlock();

    // Erase required sectors (4 KB each)
    uint16_t num_sectors = (len + 4095) / 4096;
    for (uint16_t s = 0; s < num_sectors; s++) {
        flash_erase_sector(flash_addr + (s * 4096));
    }

    // Program pages (256 bytes each)
    uint16_t num_pages = (len + 255) / 256;
    for (uint16_t p = 0; p < num_pages; p++) {
        uint16_t page_len = 256;
        if ((p + 1) * 256 > len) {
            page_len = len - (p * 256);
        }
        flash_write_page(flash_addr + (p * 256), page_len, (uint8_t *)&code_buf[p * 256]);
    }

    // Invalidate CPU instruction cache tags (256 bytes starting at _ictag_start_)
    memset((void *)&_ictag_start_, 0, 256);
    DEBUG_LOG("PROTO", "Cache invalidated. Ready to execute.");
    return true;
}

int epd_prototype_execute(uint32_t flash_addr, uint32_t *duration_ms) {
    if (flash_addr == 0) flash_addr = PROTOTYPE_CODE_ADDR;

    // Bounds and sector alignment check
    if ((flash_addr & 0xFFF) != 0 ||
        flash_addr < PROTOTYPE_CODE_ADDR ||
        flash_addr >= (PROTOTYPE_CODE_ADDR + PROTOTYPE_CODE_MAX_SIZE)) {
        DEBUG_LOG("PROTO", "Execute address 0x%05X out of bounds!", flash_addr);
        return -1;
    }

    // Safety check: ensure target flash contains valid code and is not erased/unprogrammed
    uint32_t first_word = 0;
    flash_read_page(flash_addr, sizeof(first_word), (uint8_t *)&first_word);
    if (first_word == 0xFFFFFFFF || first_word == 0x00000000) {
        DEBUG_LOG("PROTO", "Invalid entry @ 0x%05X (word=0x%08lX, unprogrammed/erased)", flash_addr, first_word);
        return -2;
    }

    // Ensure cache tags are fresh
    memset((void *)&_ictag_start_, 0, 256);

    epd_test_api_t api = {
        .struct_version = 1,
#if (BOARD == BOARD_HANSHOW_E31PA)
        .board_type = 1,
#else
        .board_type = 0,
#endif
        .write_cmd = EPD_WriteCmd,
        .write_data = EPD_WriteData,
        .read_spi = EPD_SPI_read,
        .sleep_us = sleep_us,
        .sleep_ms = proto_sleep_ms,
        .gpio_write = (void (*)(uint32_t, uint8_t))gpio_write,
        .gpio_read = (uint8_t (*)(uint32_t))gpio_read,
        .epd_power_on = proto_power_on,
        .epd_power_off = proto_power_off,
        .epd_init_pins = EPD_init_pins,
        .epd_isolate_pins = EPD_isolate_pins,
        .log = debug_log,
        .render_buffer = epd_render_buffer,
        .buffer_size = SHARED_SCRATCH_RAM_SIZE,
        .active_slot = settings.active_slot,
        .render_style = settings.render_style,
    };

    // Bank-aware XIP address translation:
    // If active firmware booted from Bank 1 (offset 0x40000), the MCU hardware
    // re-maps flash reads/fetches by -0x40000. Physical flash 0x70000 is mapped to 0x30000.
    uint32_t xip_addr = (mcuBootAddrGet() == 1) ? (flash_addr - 0x40000) : flash_addr;

    DEBUG_LOG("PROTO", "Jumping to snippet @ 0x%05X (XIP: 0x%05X, Bank %u)...",
              flash_addr, xip_addr, mcuBootAddrGet());
    uint32_t t0 = clock_time();

    epd_snippet_entry_t entry = (epd_snippet_entry_t)(xip_addr | 1);
    int ret = entry(&api);

    uint32_t dt = (clock_time() - t0) / CLOCK_16M_SYS_TIMER_CLK_1MS;
    if (duration_ms) *duration_ms = dt;

    DEBUG_LOG("PROTO", "Snippet finished with ret=%d (%lu ms)", ret, dt);
    return ret;
}
