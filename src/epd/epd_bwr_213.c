#include <stdint.h>
#include <stdbool.h>
#include "tl_common.h"
#include "app_config.h"
#include "epd.h"
#include "epd_spi.h"
#include "epd_bwr_213.h"
#include "debug_uart.h"

// UC8151 / IL0373 2.13" 3-Color (Black/White/Red) EPD Controller
// Display resolution: 250 x 122 (128x250 buffer, 4,000 bytes per plane)
// Native controller for Hanshow Stellar-M3N@ / E31HA ESL

uint8_t EPD_BWR_213_detect(void) {
    return 1;
}

uint8_t EPD_BWR_213_read_temp(void) {
    // Return latest cached temperature measured during display refresh
    // Avoids disruptive and power-expensive panel wake-up cycles on every BTHome beacon
    return epd_temperature;
}

uint8_t EPD_BWR_213_DisplayWithStyle(const uint8_t *bw_image, const uint8_t *red_image, int size, uint8_t style) {
    uint8_t epd_temperature = 22;

    if (style >= STYLE_COUNT) style = STYLE_STANDARD;

    // 1. Booster Soft Start (0x06)
    EPD_WriteCmd(0x06);
    EPD_WriteData(0x17);
    EPD_WriteData(0x17);
    EPD_WriteData(0x17);

    // 2. Power On (0x04)
    EPD_WriteCmd(0x04);
    EPD_CheckStatus(100);

    // Full Refresh 3-Color BWR OTP (~15s): Factory calibrated 3-color waveform
    DEBUG_LOG("EPD", "Panel Init: Style %u (Full BWR OTP ~15s, PSR=0x0F)", style);

    // 3. Panel Setting (PSR 0x00): Factory OTP LUT, BWR mode (0x0F)
    EPD_WriteCmd(0x00);
    EPD_WriteData(0x0F);

    // 4. VCOM & Data Interval Setting (0x50): 0x97
    EPD_WriteCmd(0x50);
    EPD_WriteData(0x97);

    DEBUG_LOG("EPD", "Loading DTM1 (BW) & DTM2 (Red) [%u B]...", size);

    // 5. DTM1 (0x10): Black/White plane (0 = Black, 1 = White)
    EPD_WriteCmd(0x10);
    for (int i = 0; i < size; i++) {
        uint8_t bw = bw_image ? bw_image[i] : 0xFF;
        uint8_t red = red_image ? red_image[i] : 0x00;
        uint8_t dtm1;

        switch (style) {
            case STYLE_STANDARD:
                dtm1 = bw | red;
                break;
            case STYLE_BW_STANDARD:
                dtm1 = (~red) & bw;
                break;
            case STYLE_BW_INVERTED:
                dtm1 = ~((~red) & bw);
                break;
            case STYLE_RW_STANDARD:
            case STYLE_RW_INVERTED:
                dtm1 = 0xFF; // BW plane all white; red plane drives artwork
                break;
            default:
                dtm1 = bw | red;
                break;
        }

        if ((i & 0x0F) == 15) dtm1 |= 0x3F; // Pad bits high
        EPD_WriteData(dtm1);
    }

    // 6. DTM2 (0x13): Red plane (1 = Red, 0 = Non-red)
    EPD_WriteCmd(0x13);
    for (int i = 0; i < size; i++) {
        uint8_t bw = bw_image ? bw_image[i] : 0xFF;
        uint8_t red = red_image ? red_image[i] : 0x00;
        uint8_t dtm2;

        switch (style) {
            case STYLE_STANDARD:
                dtm2 = red;
                break;
            case STYLE_BW_STANDARD:
            case STYLE_BW_INVERTED:
                dtm2 = 0x00; // Zero red output
                break;
            case STYLE_RW_STANDARD:
                dtm2 = ~((~red) & bw); // Black & Red become Red; White stays White
                break;
            case STYLE_RW_INVERTED:
                dtm2 = (~red) & bw;    // White becomes Red; Black & Red become White
                break;
            default:
                dtm2 = red;
                break;
        }

        if ((i & 0x0F) == 15) dtm2 &= 0xC0; // Pad bits cleared
        EPD_WriteData(dtm2);
    }

    // Trigger Display Refresh (DRF: 0x12)
    DEBUG_LOG("EPD", "Triggering DRF (0x12) refresh...");
    EPD_WriteCmd(0x12);

    // Wait until BUSY (PA1) transitions LOW (up to 100ms)
    unsigned long t0 = clock_time();
    while ((gpio_read(GPIO_EPD_BUSY) != 0) && (clock_time() - t0 < 100 * CLOCK_16M_SYS_TIMER_CLK_1MS)) {
        WaitMs(1);
    }

    return epd_temperature;
}
