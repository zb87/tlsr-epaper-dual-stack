#ifndef _EPD_SPI_H_
#define _EPD_SPI_H_

#include <stdint.h>
#include "app_config.h"

// Power Gate: Active Low MOSFET (PB7 on Stellar-XL3N@ 4.2", PC5 on Stellar-M3N@ 2.13")
#if (BOARD == BOARD_HANSHOW_E31PA)
#define EPD_POWER_ON() do { \
    gpio_set_output_en(GPIO_EPD_PWR_ENABLE, 1); \
    gpio_setup_up_down_resistor(GPIO_EPD_PWR_ENABLE, PM_PIN_UP_DOWN_FLOAT); \
    gpio_write(GPIO_EPD_PWR_ENABLE, 0); \
} while (0)

#define EPD_POWER_OFF() do { \
    gpio_write(GPIO_EPD_PWR_ENABLE, 1); \
    gpio_set_output_en(GPIO_EPD_PWR_ENABLE, 0); \
    gpio_setup_up_down_resistor(GPIO_EPD_PWR_ENABLE, PM_PIN_PULLUP_10K); \
} while (0)
#else
#define EPD_POWER_ON() do { \
    gpio_set_output_en(GPIO_EPD_PWR_ENABLE, 1); \
    gpio_setup_up_down_resistor(GPIO_EPD_PWR_ENABLE, PM_PIN_PULLDOWN_100K); \
    gpio_write(GPIO_EPD_PWR_ENABLE, 0); \
} while (0)

#define EPD_POWER_OFF() do { \
    gpio_setup_up_down_resistor(GPIO_EPD_PWR_ENABLE, PM_PIN_PULLUP_1M); \
    gpio_write(GPIO_EPD_PWR_ENABLE, 1); \
} while (0)
#endif

// Command / Data Select: PD7
#define EPD_ENABLE_WRITE_CMD()    gpio_write(GPIO_EPD_DC, 0)
#define EPD_ENABLE_WRITE_DATA()   gpio_write(GPIO_EPD_DC, 1)

// Busy line: PA1 (!gpio_read indicates busy status on 2.13" panel in ATC_TLSR_Paper)
#define EPD_IS_BUSY()             (!gpio_read(GPIO_EPD_BUSY))

void EPD_init_pins(void);
void EPD_isolate_pins(void);
void EPD_SPI_Write(uint8_t value);
uint8_t EPD_SPI_read(void);
void EPD_WriteCmd(uint8_t cmd);
void EPD_WriteData(uint8_t data);
void EPD_CheckStatus(int max_ms);
void EPD_CheckStatus_inverted(int max_ms);
void EPD_send_lut(const uint8_t lut[], int len);
void EPD_send_empty_lut(uint8_t lut, int len);
void EPD_LoadImage(const uint8_t *image, int size, uint8_t cmd);
void epd_delay_ms(uint32_t ms);
void ble_display_poll(void);

#endif // _EPD_SPI_H_
