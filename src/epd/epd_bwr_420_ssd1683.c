#include <stdint.h>
#include <stdbool.h>
#include "tl_common.h"
#include "app_config.h"
#include "epd.h"
#include "epd_spi.h"
#include "epd_bwr_420_ssd1683.h"
#include "debug_uart.h"

// Solomon Systech SSD1683 4.2" 3-Color (Black/White/Red) EPD Controller
// Native controller for BLOZI Endor 4.2" ESL (Endor-Telink-0420)
// Panel resolution: 400 x 300 pixels (50 bytes/row, 15,000 bytes per plane)

uint8_t EPD_SSD1683_detect(void) {
    return 1;
}

uint8_t EPD_SSD1683_read_temp(void) {
    // Return latest cached temperature measured during EPD_SSD1683_init()
    return epd_temperature;
}

static void ssd1683_set_cursor_origin(void) {
    EPD_WriteCmd(0x4E); // Set RAM X address counter
    EPD_WriteData(0x00);
    EPD_WriteCmd(0x4F); // Set RAM Y address counter
    EPD_WriteData(0x00);
    EPD_WriteData(0x00);
}

void EPD_SSD1683_init(void) {
    // Hardware Reset (Active LOW)
    gpio_write(GPIO_EPD_RESET, 0);
    epd_delay_ms(10);
    gpio_write(GPIO_EPD_RESET, 1);
    epd_delay_ms(10);
    EPD_CheckStatus(200);

    // Software Reset (0x12)
    EPD_WriteCmd(0x12);
    EPD_CheckStatus(200);

    // Driver Output Control (0x01): 300 gates (0x012B = 299), TB=0, SM=0, GD=0
    EPD_WriteCmd(0x01);
    EPD_WriteData(0x2B);
    EPD_WriteData(0x01);
    EPD_WriteData(0x00);

    // Data Entry Mode (0x11): X increment, Y increment (top-to-bottom, left-to-right)
    EPD_WriteCmd(0x11);
    EPD_WriteData(0x03);

    // Set RAM X-Address Start/End Position (0x44): 0..49 (50 bytes = 400 px)
    EPD_WriteCmd(0x44);
    EPD_WriteData(0x00);
    EPD_WriteData(0x31);

    // Set RAM Y-Address Start/End Position (0x45): 0..299 (300 rows)
    EPD_WriteCmd(0x45);
    EPD_WriteData(0x00);
    EPD_WriteData(0x00);
    EPD_WriteData(0x2B);
    EPD_WriteData(0x01);

    // Border Waveform Control (0x3C)
    EPD_WriteCmd(0x3C);
    EPD_WriteData(0x05);

    // Display Update Control 1 (0x21): Normal BW & Red RAM, Source S0..S399
    EPD_WriteCmd(0x21);
    EPD_WriteData(0x00);
    EPD_WriteData(0x80);

    // Temperature Sensor Control (0x18): Internal temperature sensor
    EPD_WriteCmd(0x18);
    EPD_WriteData(0x80);

    // Load Temperature & Waveform LUT from OTP (0x22 = 0xB1, Master Activation 0x20)
    EPD_WriteCmd(0x22);
    EPD_WriteData(0xB1);
    EPD_WriteCmd(0x20);
    EPD_CheckStatus(200);

    // Read internal temperature register (0x1B)
    EPD_WriteCmd(0x1B);
    uint8_t raw_temp = EPD_SPI_read();
    EPD_SPI_read();
    if (raw_temp > 0 && raw_temp <= 80) {
        epd_temperature = raw_temp;
    }

    ssd1683_set_cursor_origin();
}

void EPD_SSD1683_start_dtm1(void) {
    ssd1683_set_cursor_origin();
    EPD_WriteCmd(0x24); // Write RAM (BW): 0 = Black, 1 = White
}

void EPD_SSD1683_start_dtm2(void) {
    ssd1683_set_cursor_origin();
    EPD_WriteCmd(0x26); // Write RAM (RED): 1 = Red, 0 = Non-red
}

void EPD_SSD1683_refresh(void) {
    DEBUG_LOG("EPD", "SSD1683: Triggering Display Update (0x22=0xF7, 0x20) 400x300...");
    EPD_WriteCmd(0x22);
    EPD_WriteData(0xF7);
    EPD_WriteCmd(0x20);

    // Wait up to 100ms for BUSY (PC6) to assert HIGH
    unsigned long t0 = clock_time();
    while (!EPD_IS_BUSY() && (clock_time() - t0 < 100 * CLOCK_16M_SYS_TIMER_CLK_1MS)) {
        epd_delay_ms(1);
    }
}

void EPD_SSD1683_set_sleep(void) {
    // Deep Sleep Mode 1 (0x10 -> 0x01)
    EPD_WriteCmd(0x10);
    EPD_WriteData(0x01);
}
