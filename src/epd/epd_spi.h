#ifndef _EPD_SPI_H_
#define _EPD_SPI_H_

#include <stdint.h>
#include "app_config.h"

// Power Gate: Active Low MOSFET (PB7 on Stellar-XL3N@ 4.2", PC5 on Stellar-M3N@ 2.13", none on Endor 0420)
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
#elif (BOARD == BOARD_ENDOR_0420) || (BOARD == BOARD_ENDOR_0750)
#define EPD_POWER_ON()  do {} while (0)
#define EPD_POWER_OFF() do {} while (0)
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

// Command / Data Select
#define EPD_ENABLE_WRITE_CMD()    gpio_write(GPIO_EPD_DC, 0)
#define EPD_ENABLE_WRITE_DATA()   gpio_write(GPIO_EPD_DC, 1)

// Busy line polarity: SSD1683 (Endor 0420) is Active HIGH; UC8151/UC8176 are Active LOW
#if (BOARD == BOARD_ENDOR_0420)
#define EPD_IS_BUSY()             (gpio_read(GPIO_EPD_BUSY) != 0)
#define EPD_BUSY_WAKEUP_LEVEL     Level_Low
#else
#define EPD_IS_BUSY()             (!gpio_read(GPIO_EPD_BUSY))
#define EPD_BUSY_WAKEUP_LEVEL     Level_High
#endif

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
