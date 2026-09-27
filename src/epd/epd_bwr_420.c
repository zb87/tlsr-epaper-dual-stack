#include <stdint.h>
#include <stdbool.h>
#include "tl_common.h"
#include "app_config.h"
#include "epd.h"
#include "epd_spi.h"
#include "epd_bwr_420.h"
#include "debug_uart.h"

// UC8176 4.2" 3-Color (Black/White/Red) EPD Controller
// Native controller for Hanshow Stellar-XL3N@ / E31PA ESL
// Panel resolution: 400 x 300 pixels (50 bytes/row, 15,000 bytes per plane)

uint8_t EPD_BWR_420_detect(void) {
    return 1;
}

uint8_t EPD_BWR_420_read_temp(void) {
    // Return latest cached temperature measured during EPD_BWR_420_init()
    // Avoids disruptive and power-expensive panel wake-up cycles
    return epd_temperature;
}

void EPD_BWR_420_init(void) {
    // 1. Power On (0x04) and wait for charge pump ready (PA1 HIGH)
    EPD_WriteCmd(0x04);
    EPD_CheckStatus(200);

    // Read internal temperature sensor (0x40) while panel is powered on
    EPD_WriteCmd(0x40);
    EPD_CheckStatus(100);
    uint8_t raw_temp = EPD_SPI_read();
    EPD_SPI_read();
    if (raw_temp > 0 && raw_temp <= 80) {
        epd_temperature = raw_temp;
    }

    // 2. Panel Setting (PSR 0x00): 2 bytes
    // Byte 1: 0x0F -> RES=400x300, OTP LUT, BWR mode, Gate scan up (UD=1), Source right, Booster ON
    // Byte 2: 0x0D -> VCM_HZ / scan settings
    EPD_WriteCmd(0x00);
    EPD_WriteData(0x0F);
    EPD_WriteData(0x0D);

    // 3. Booster Soft Start (0x06): 4.2" high-capacity driving strength
    EPD_WriteCmd(0x06);
    EPD_WriteData(0xD7);
    EPD_WriteData(0xD7);
    EPD_WriteData(0x3F);

    // 4. VCOM & Data Interval Setting (CDI 0x50): 0x77
    EPD_WriteCmd(0x50);
    EPD_WriteData(0x77);
}

void EPD_BWR_420_start_dtm1(void) {
    EPD_WriteCmd(0x10); // DTM1: Black/White Plane
}

void EPD_BWR_420_start_dtm2(void) {
    EPD_WriteCmd(0x13); // DTM2: Red Plane
}

void EPD_BWR_420_refresh(void) {
    // Trigger Display Refresh (DRF: 0x12)
    DEBUG_LOG("EPD", "UC8176: Triggering DRF (0x12) 400x300 refresh...");
    EPD_WriteCmd(0x12);
    epd_delay_ms(100); // 100ms wait matching factory firmware 0x3DB78 to allow controller to enter active refresh
}

void EPD_BWR_420_set_sleep(void) {
    // Float VCOM before power off
    EPD_WriteCmd(0x50);
    EPD_WriteData(0xF7);

    // Power off panel and enter deep sleep
    EPD_WriteCmd(0x02); // Power off
    EPD_CheckStatus(100);
    EPD_WriteCmd(0x07); // Deep sleep
    EPD_WriteData(0xA5);
}
