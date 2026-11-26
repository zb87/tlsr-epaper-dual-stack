#ifndef _EXT_FLASH_H_
#define _EXT_FLASH_H_

#include <stdint.h>
#include <stdbool.h>
#include "app_config.h"

#if EXT_FLASH_ENABLED

/**
 * Initialize external SPI NOR flash (GD25Q16EQ), verify JEDEC ID,
 * and enter Deep Power-Down (0xB9) until accessed.
 */
void ext_flash_init(void);

/**
 * Wake external SPI flash from Deep Power-Down (0xAB) and configure HW SPI.
 */
void ext_flash_wake(void);

/**
 * Put external SPI flash into Deep Power-Down (0xB9) and float SPI bus pins.
 */
void ext_flash_sleep(void);

/**
 * Return true if external SPI flash is currently awake.
 */
bool ext_flash_is_awake(void);

/**
 * Read bytes from external SPI flash (auto-wakes/sleeps if not already awake).
 */
void ext_flash_read(uint32_t addr, uint32_t len, uint8_t *buf);

/**
 * Erase a 4 KB sector (0x20) in external SPI flash.
 */
void ext_flash_erase_sector(uint32_t addr);

/**
 * Erase a 64 KB block (0xD8) in external SPI flash.
 */
void ext_flash_erase_block64k(uint32_t addr);

/**
 * Write bytes to external SPI flash, splitting across 256-byte page boundaries.
 */
void ext_flash_write(uint32_t addr, uint32_t len, const uint8_t *buf);

#else

static inline void ext_flash_init(void) {}
static inline void ext_flash_wake(void) {}
static inline void ext_flash_sleep(void) {}
static inline bool ext_flash_is_awake(void) { return false; }
static inline void ext_flash_read(uint32_t addr, uint32_t len, uint8_t *buf) {
    (void)addr; (void)len; (void)buf;
}
static inline void ext_flash_erase_sector(uint32_t addr) { (void)addr; }
static inline void ext_flash_erase_block64k(uint32_t addr) { (void)addr; }
static inline void ext_flash_write(uint32_t addr, uint32_t len, const uint8_t *buf) {
    (void)addr; (void)len; (void)buf;
}

#endif // EXT_FLASH_ENABLED

#endif // _EXT_FLASH_H_
