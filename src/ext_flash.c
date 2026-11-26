#include "tl_common.h"
#include "app_config.h"

#if EXT_FLASH_ENABLED

#include "spi.h"
#include "ext_flash.h"
#include "led.h"
#include "debug_uart.h"

RAM static bool s_ext_flash_awake = false;

static void ext_flash_write_enable(void) {
    uint8_t cmd = 0x06; // WREN
    spi_write(&cmd, 1, NULL, 0, EXT_FLASH_CS_PIN);
}

static uint8_t ext_flash_read_status(void) {
    uint8_t cmd = 0x05; // RDSR
    uint8_t status = 0;
    spi_read(&cmd, 1, &status, 1, EXT_FLASH_CS_PIN);
    return status;
}

static void ext_flash_wait_busy(uint32_t max_ms) {
    uint32_t start = clock_time();
    while (ext_flash_read_status() & 0x01) {
        if (clock_time_exceed(start, max_ms * 1000)) {
            DEBUG_LOG("EFLASH", "Busy timeout after %lu ms", (unsigned long)max_ms);
            break;
        }
        // Do not call ble_display_poll()/blt_sdk_main_loop() here: flash erases run inside ATT write callbacks
        sleep_us(500);
    }
}

void ext_flash_wake(void) {
    if (s_ext_flash_awake) {
        return;
    }

    reg_clk_en0 |= FLD_CLK0_SPI_EN;

    // 2 MHz SPI Mode 0 @ 24 MHz system clock
    spi_master_init(SPI_CLK_2M, SPI_MODE0);
    spi_master_gpio_set(EXT_FLASH_SPI_GROUP);
    spi_masterCSpin_select(EXT_FLASH_CS_PIN);
    // spi_master_gpio_set(B6B7D2D7) sets PD2 as CS, which clobbers LED_BLUE; restore LED state
    led_set(led_get_state());

    uint8_t cmd = 0xAB; // Release from Deep Power-Down
    spi_write(&cmd, 1, NULL, 0, EXT_FLASH_CS_PIN);
    WaitUs(50); // tRES1 max 20 us for GD25Q16EQ

    s_ext_flash_awake = true;
}

void ext_flash_sleep(void) {
    if (!s_ext_flash_awake) {
        return;
    }

    ext_flash_wait_busy(100);

    uint8_t cmd = 0xB9; // Deep Power-Down
    spi_write(&cmd, 1, NULL, 0, EXT_FLASH_CS_PIN);
    WaitUs(20); // tDP max 3 us

    // Keep CS# HIGH via 1M pull-up and output driver
    gpio_write(EXT_FLASH_CS_PIN, 1);
    gpio_set_func(EXT_FLASH_CS_PIN, AS_GPIO);
    gpio_set_output_en(EXT_FLASH_CS_PIN, 1);
    gpio_set_input_en(EXT_FLASH_CS_PIN, 0);
    gpio_setup_up_down_resistor(EXT_FLASH_CS_PIN, PM_PIN_PULLUP_1M);

    // Float SPI data/clock lines to prevent standby current leakage
    gpio_set_func(GPIO_PB6, AS_GPIO);
    gpio_set_output_en(GPIO_PB6, 0);
    gpio_set_input_en(GPIO_PB6, 0);
    gpio_setup_up_down_resistor(GPIO_PB6, PM_PIN_UP_DOWN_FLOAT);

    gpio_set_func(GPIO_PB7, AS_GPIO);
    gpio_set_output_en(GPIO_PB7, 0);
    gpio_set_input_en(GPIO_PB7, 0);
    gpio_setup_up_down_resistor(GPIO_PB7, PM_PIN_UP_DOWN_FLOAT);

    gpio_set_func(GPIO_PD7, AS_GPIO);
    gpio_set_output_en(GPIO_PD7, 0);
    gpio_set_input_en(GPIO_PD7, 0);
    gpio_setup_up_down_resistor(GPIO_PD7, PM_PIN_UP_DOWN_FLOAT);

    led_set(led_get_state());
    reg_clk_en0 &= ~FLD_CLK0_SPI_EN;

    s_ext_flash_awake = false;
}

bool ext_flash_is_awake(void) {
    return s_ext_flash_awake;
}

void ext_flash_init(void) {
    s_ext_flash_awake = false;
    ext_flash_wake();

    uint8_t cmd = 0x9F; // Read JEDEC ID
    uint8_t jedec[3] = {0};
    spi_read(&cmd, 1, jedec, sizeof(jedec), EXT_FLASH_CS_PIN);
    uint8_t sr1 = ext_flash_read_status();
    if (sr1 & 0x7C) {
        // Clear any factory/legacy block protection bits before erase/write
        ext_flash_write_enable();
        uint8_t wrsr[2] = {0x01, 0x00};
        spi_write(wrsr, sizeof(wrsr), NULL, 0, EXT_FLASH_CS_PIN);
        ext_flash_wait_busy(50);
        sr1 = ext_flash_read_status();
    }
    DEBUG_LOG("EFLASH", "JEDEC ID: %02X %02X %02X, SR1: 0x%02X",
              jedec[0], jedec[1], jedec[2], sr1);

    ext_flash_sleep();
}

void ext_flash_read(uint32_t addr, uint32_t len, uint8_t *buf) {
    if (!buf || len == 0 || addr >= EXT_FLASH_SIZE_BYTES) {
        return;
    }
    if (addr + len > EXT_FLASH_SIZE_BYTES) {
        len = EXT_FLASH_SIZE_BYTES - addr;
    }

    bool need_sleep = false;
    if (!s_ext_flash_awake) {
        ext_flash_wake();
        need_sleep = true;
    }

    while (len > 0) {
        uint32_t chunk = (len > 256) ? 256 : len;
        uint8_t cmd[4] = {
            0x03,
            (uint8_t)((addr >> 16) & 0xFF),
            (uint8_t)((addr >> 8) & 0xFF),
            (uint8_t)(addr & 0xFF)
        };
        spi_read(cmd, sizeof(cmd), buf, (int)chunk, EXT_FLASH_CS_PIN);
        addr += chunk;
        buf += chunk;
        len -= chunk;
    }

    if (need_sleep) {
        ext_flash_sleep();
    }
}

void ext_flash_erase_sector(uint32_t addr) {
    if (addr >= EXT_FLASH_SIZE_BYTES) {
        return;
    }

    bool need_sleep = false;
    if (!s_ext_flash_awake) {
        ext_flash_wake();
        need_sleep = true;
    }

    ext_flash_write_enable();
    uint8_t cmd[4] = {
        0x20, // 4 KB Sector Erase
        (uint8_t)((addr >> 16) & 0xFF),
        (uint8_t)((addr >> 8) & 0xFF),
        (uint8_t)(addr & 0xFF)
    };
    spi_write(cmd, sizeof(cmd), NULL, 0, EXT_FLASH_CS_PIN);
    ext_flash_wait_busy(500);

    if (need_sleep) {
        ext_flash_sleep();
    }
}

void ext_flash_erase_block64k(uint32_t addr) {
    if (addr >= EXT_FLASH_SIZE_BYTES) {
        return;
    }

    bool need_sleep = false;
    if (!s_ext_flash_awake) {
        ext_flash_wake();
        need_sleep = true;
    }

    ext_flash_write_enable();
    uint8_t cmd[4] = {
        0xD8, // 64 KB Block Erase
        (uint8_t)((addr >> 16) & 0xFF),
        (uint8_t)((addr >> 8) & 0xFF),
        (uint8_t)(addr & 0xFF)
    };
    spi_write(cmd, sizeof(cmd), NULL, 0, EXT_FLASH_CS_PIN);
    ext_flash_wait_busy(2000);

    if (need_sleep) {
        ext_flash_sleep();
    }
}

void ext_flash_write(uint32_t addr, uint32_t len, const uint8_t *buf) {
    if (!buf || len == 0 || addr >= EXT_FLASH_SIZE_BYTES) {
        return;
    }
    if (addr + len > EXT_FLASH_SIZE_BYTES) {
        len = EXT_FLASH_SIZE_BYTES - addr;
    }

    bool need_sleep = false;
    if (!s_ext_flash_awake) {
        ext_flash_wake();
        need_sleep = true;
    }

    while (len > 0) {
        uint32_t page_off = addr & 0xFF;
        uint32_t chunk = 256 - page_off;
        if (chunk > len) {
            chunk = len;
        }

        ext_flash_write_enable();
        uint8_t cmd[4] = {
            0x02, // Page Program
            (uint8_t)((addr >> 16) & 0xFF),
            (uint8_t)((addr >> 8) & 0xFF),
            (uint8_t)(addr & 0xFF)
        };
        spi_write(cmd, sizeof(cmd), (unsigned char *)buf, (int)chunk, EXT_FLASH_CS_PIN);
        ext_flash_wait_busy(50);

        addr += chunk;
        buf += chunk;
        len -= chunk;
    }

    if (need_sleep) {
        ext_flash_sleep();
    }
}

#endif // EXT_FLASH_ENABLED
