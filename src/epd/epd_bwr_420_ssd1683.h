#ifndef _EPD_BWR_420_SSD1683_H_
#define _EPD_BWR_420_SSD1683_H_

#include <stdint.h>
#include <stdbool.h>

// Solomon Systech SSD1683 4.2" 3-Color (Black/White/Red) EPD Controller
// Display resolution: 400 x 300 (50 bytes/line, 15,000 bytes per plane)
// Native controller for BLOZI Endor 4.2" ESL (Endor-Telink-0420)

uint8_t EPD_SSD1683_detect(void);
uint8_t EPD_SSD1683_read_temp(void);
void EPD_SSD1683_init(void);
void EPD_SSD1683_start_dtm1(void);
void EPD_SSD1683_start_dtm2(void);
void EPD_SSD1683_refresh(void);
void EPD_SSD1683_set_sleep(void);

#endif // _EPD_BWR_420_SSD1683_H_
