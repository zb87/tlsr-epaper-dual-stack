#include <stdint.h>
#include <stdbool.h>
#include "tl_common.h"
#include "app_config.h"
#include "epd.h"
#include "epd_spi.h"
#include "epd_bwr_750.h"
#include "debug_uart.h"

// UC8179 7.5" 3-Color (Black/White/Red) EPD Controller
// Native controller for BLOZI Endor 7.5" ESL (Endor-Telink-0750)
// Panel resolution: 800 x 480 pixels (100 bytes/row, 48,000 bytes per plane)

uint8_t EPD_BWR_750_detect(void) {
    return 1;
}

uint8_t EPD_BWR_750_read_temp(void) {
    // Return latest cached temperature measured during EPD_BWR_750_init()
    return epd_temperature;
}

void EPD_BWR_750_init(void) {
    // Hardware Reset (Active LOW: 10 ms LOW, 10 ms recovery)
    gpio_write(GPIO_EPD_RESET, 0);
    epd_delay_ms(10);
    gpio_write(GPIO_EPD_RESET, 1);
    epd_delay_ms(10);
    EPD_CheckStatus(200);

    // Panel Setting (PSR 0x00): 0x0F -> OTP LUT, 800x480 BWR 3-color mode
    EPD_WriteCmd(0x00);
    EPD_WriteData(0x0F);

    // VCOM & Data Interval Setting (CDI 0x50): 0x20, 0x07 (matches stock Endor-0750)
    // DDX=00 -> DTM1: 0=White, 1=Black; DTM2: 0=Red, 1=Non-red
    EPD_WriteCmd(0x50);
    EPD_WriteData(0x20);
    EPD_WriteData(0x07);
}

void EPD_BWR_750_start_dtm1(void) {
    EPD_WriteCmd(0x10); // DTM1: Black/White Plane
}

void EPD_BWR_750_start_dtm2(void) {
    EPD_WriteCmd(0x13); // DTM2: Red Plane
}

void EPD_BWR_750_refresh(void) {
    // Power On (0x04) after uploading DTM1 + DTM2, then wait for boost ready (PC6 HIGH)
    EPD_WriteCmd(0x04);
    EPD_CheckStatus(500);
    DEBUG_LOG("EPD", "UC8179: PON done, BUSY=%d", EPD_IS_BUSY() ? 1 : 0);

    // Read internal temperature sensor (0x40) while booster is active
    EPD_WriteCmd(0x40);
    EPD_CheckStatus(100);
    uint8_t raw_temp = EPD_SPI_read();
    if (raw_temp > 0 && raw_temp <= 80) {
        epd_temperature = raw_temp;
    }

    DEBUG_LOG("EPD", "UC8179: Triggering DRF (0x12) 800x480 refresh (T=%uC)...", epd_temperature);
    EPD_WriteCmd(0x12);
    gpio_write(GPIO_EPD_CS, 1); // Idle CS HIGH during refresh

    // Wait up to 200 ms for BUSY (PC6) to assert LOW
    unsigned long t0 = clock_time();
    while (!EPD_IS_BUSY() && (clock_time() - t0 < 200 * CLOCK_16M_SYS_TIMER_CLK_1MS)) {
        epd_delay_ms(1);
    }
    DEBUG_LOG("EPD", "UC8179: DRF sent, BUSY=%d", EPD_IS_BUSY() ? 1 : 0);
}

void EPD_BWR_750_set_sleep(void) {
    // Power off panel and enter deep sleep
    EPD_WriteCmd(0x02); // Power off
    EPD_CheckStatus(500);
    EPD_WriteCmd(0x07); // Deep sleep
    EPD_WriteData(0xA5);
    epd_delay_ms(10);
    gpio_write(GPIO_EPD_CS, 1);
}
