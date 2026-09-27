#ifndef _EPD_BWR_420_H_
#define _EPD_BWR_420_H_

#include <stdint.h>
#include <stdbool.h>

// UC8176 4.2" 3-Color (Black/White/Red) EPD Controller
// Display resolution: 400 x 300 (50 bytes/line, 15,000 bytes per plane)
// Native controller for Hanshow Stellar-XL3N@ / E31PA ESL

uint8_t EPD_BWR_420_detect(void);
uint8_t EPD_BWR_420_read_temp(void);
void EPD_BWR_420_init(void);
void EPD_BWR_420_start_dtm1(void);
void EPD_BWR_420_start_dtm2(void);
void EPD_BWR_420_refresh(void);
void EPD_BWR_420_set_sleep(void);

#endif // _EPD_BWR_420_H_
