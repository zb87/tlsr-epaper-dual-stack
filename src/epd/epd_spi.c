#include "tl_common.h"
#include "app_config.h"
#include "epd_spi.h"

void EPD_init_pins(void) {
    // Reset: PD4
    gpio_set_func(GPIO_EPD_RESET, AS_GPIO);
    gpio_set_output_en(GPIO_EPD_RESET, 1);
    gpio_set_input_en(GPIO_EPD_RESET, 0);
    gpio_setup_up_down_resistor(GPIO_EPD_RESET, PM_PIN_PULLUP_1M);
    gpio_write(GPIO_EPD_RESET, 1);

    // D/C#: PD7
    gpio_set_func(GPIO_EPD_DC, AS_GPIO);
    gpio_set_output_en(GPIO_EPD_DC, 1);
    gpio_set_input_en(GPIO_EPD_DC, 0);
    gpio_setup_up_down_resistor(GPIO_EPD_DC, PM_PIN_PULLUP_1M);

    // Busy: PA1 (Input)
    gpio_set_func(GPIO_EPD_BUSY, AS_GPIO);
    gpio_set_output_en(GPIO_EPD_BUSY, 0);
    gpio_set_input_en(GPIO_EPD_BUSY, 1);
#if (BOARD == BOARD_HANSHOW_E31PA)
    gpio_setup_up_down_resistor(GPIO_EPD_BUSY, PM_PIN_UP_DOWN_FLOAT);
#else
    gpio_setup_up_down_resistor(GPIO_EPD_BUSY, PM_PIN_PULLUP_1M);
#endif

    // CS: PB4
    gpio_set_func(GPIO_EPD_CS, AS_GPIO);
    gpio_set_output_en(GPIO_EPD_CS, 1);
    gpio_set_input_en(GPIO_EPD_CS, 0);
    gpio_setup_up_down_resistor(GPIO_EPD_CS, PM_PIN_PULLUP_1M);
    gpio_write(GPIO_EPD_CS, 1);

    // CLK: PB5
    gpio_set_func(GPIO_EPD_CLK, AS_GPIO);
    gpio_set_output_en(GPIO_EPD_CLK, 1);
    gpio_set_input_en(GPIO_EPD_CLK, 0);
    gpio_setup_up_down_resistor(GPIO_EPD_CLK, PM_PIN_PULLUP_1M);

    // MOSI: PB6
    gpio_set_func(GPIO_EPD_MOSI, AS_GPIO);
    gpio_set_output_en(GPIO_EPD_MOSI, 1);
    gpio_set_input_en(GPIO_EPD_MOSI, 0);
    gpio_setup_up_down_resistor(GPIO_EPD_MOSI, PM_PIN_PULLUP_1M);

    // Power Gate: Active Low MOSFET (PB7 on E31PA, PC5 on E31HA)
    gpio_set_func(GPIO_EPD_PWR_ENABLE, AS_GPIO);
    gpio_set_output_en(GPIO_EPD_PWR_ENABLE, 1);
    gpio_set_input_en(GPIO_EPD_PWR_ENABLE, 0);
    EPD_POWER_OFF(); // Default OFF (pullup, pin HIGH)

    // Clock gating: enable SPI clock only while communicating with EPD
    reg_clk_en0 |= FLD_CLK0_SPI_EN;
}

void EPD_isolate_pins(void) {
    // 1. Disable internal pull-ups on all EPD pins to prevent resistor leakage
#if (BOARD == BOARD_HANSHOW_E31PA)
    gpio_setup_up_down_resistor(GPIO_EPD_PWR_ENABLE, PM_PIN_PULLUP_10K);
#else
    gpio_setup_up_down_resistor(GPIO_EPD_PWR_ENABLE, PM_PIN_PULLUP_1M);
#endif
    gpio_setup_up_down_resistor(GPIO_EPD_RESET, PM_PIN_UP_DOWN_FLOAT);
    gpio_setup_up_down_resistor(GPIO_EPD_DC,    PM_PIN_UP_DOWN_FLOAT);
    gpio_setup_up_down_resistor(GPIO_EPD_BUSY,  PM_PIN_UP_DOWN_FLOAT);
    gpio_setup_up_down_resistor(GPIO_EPD_CS,    PM_PIN_UP_DOWN_FLOAT);
    gpio_setup_up_down_resistor(GPIO_EPD_CLK,   PM_PIN_UP_DOWN_FLOAT);
    gpio_setup_up_down_resistor(GPIO_EPD_MOSI,  PM_PIN_UP_DOWN_FLOAT);

    // 2. Drive outputs to LOW (0V) to prevent forward-biasing UC8151 internal ESD clamp diodes
    gpio_write(GPIO_EPD_RESET, 0);
    gpio_write(GPIO_EPD_DC, 0);
    gpio_write(GPIO_EPD_CS, 0);
    gpio_write(GPIO_EPD_CLK, 0);
    gpio_write(GPIO_EPD_MOSI, 0);

    // Clock gating: disable SPI clock during sleep/idle
    reg_clk_en0 &= ~FLD_CLK0_SPI_EN;
}

void EPD_SPI_Write(uint8_t value) {
    uint8_t bit_clk = GPIO_EPD_CLK & 0xff;   // PB5 (bit 5: 0x20)
    uint8_t bit_mosi = GPIO_EPD_MOSI & 0xff; // PB6 (bit 6: 0x40)
    volatile uint8_t *p_out = &reg_gpio_out(GPIO_EPD_CLK);

    for (uint8_t i = 0; i < 8; i++) {
        // Drop CLK LOW and set MOSI data bit simultaneously in a single register write
        if (value & 0x80) {
            *p_out = (*p_out & ~bit_clk) | bit_mosi;
        } else {
            *p_out = *p_out & ~(bit_clk | bit_mosi);
        }
        value <<= 1;
        // Raise CLK HIGH
        *p_out |= bit_clk;
    }
}

uint8_t EPD_SPI_read(void) {
    uint8_t value = 0;

    gpio_set_output_en(GPIO_EPD_MOSI, 0);
    gpio_set_input_en(GPIO_EPD_MOSI, 1);
    gpio_write(GPIO_EPD_CS, 0);
    EPD_ENABLE_WRITE_DATA();

    for (uint8_t i = 0; i < 8; i++) {
        gpio_write(GPIO_EPD_CLK, 0);
        gpio_write(GPIO_EPD_CLK, 1);
        value = (value << 1) | (gpio_read(GPIO_EPD_MOSI) ? 1 : 0);
    }

    gpio_set_output_en(GPIO_EPD_MOSI, 1);
    gpio_set_input_en(GPIO_EPD_MOSI, 0);
    gpio_write(GPIO_EPD_CS, 1);
    return value;
}

void EPD_WriteCmd(uint8_t cmd) {
    gpio_write(GPIO_EPD_CS, 0);
    EPD_ENABLE_WRITE_CMD();
    EPD_SPI_Write(cmd);
    gpio_write(GPIO_EPD_CS, 1);
}

void EPD_WriteData(uint8_t data) {
    gpio_write(GPIO_EPD_CS, 0);
    EPD_ENABLE_WRITE_DATA();
    EPD_SPI_Write(data);
    gpio_write(GPIO_EPD_CS, 1);
}

void epd_delay_ms(uint32_t ms) {
    uint32_t start = clock_time();
    while (!clock_time_exceed(start, ms * 1000)) {
        ble_display_poll();
        sleep_us(1000);
    }
}

void EPD_CheckStatus(int max_ms) {
    unsigned long timeout_start = clock_time();
    epd_delay_ms(1);
    while (EPD_IS_BUSY()) {
        ble_display_poll();
        if (clock_time_exceed(timeout_start, max_ms * 1000))
            return;
        sleep_us(1000);
    }
}

void EPD_CheckStatus_inverted(int max_ms) {
    unsigned long timeout_start = clock_time();
    epd_delay_ms(1);
    while (!EPD_IS_BUSY()) {
        ble_display_poll();
        if (clock_time_exceed(timeout_start, max_ms * 1000))
            return;
        sleep_us(1000);
    }
}

void EPD_send_lut(const uint8_t lut[], int len) {
    EPD_WriteCmd(lut[0]);
    for (int r = 1; r < len; r++) {
        EPD_WriteData(lut[r]);
    }
}

void EPD_send_empty_lut(uint8_t lut, int len) {
    EPD_WriteCmd(lut);
    for (int r = 0; r < len; r++) {
        EPD_WriteData(0x00);
    }
}

void EPD_LoadImage(const uint8_t *image, int size, uint8_t cmd) {
    EPD_WriteCmd(cmd);
    for (int i = 0; i < size; i++) {
        EPD_WriteData(image[i]);
    }
    WaitMs(2);
}
