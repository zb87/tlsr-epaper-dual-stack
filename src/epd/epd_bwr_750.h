#ifndef _EPD_BWR_750_H_
#define _EPD_BWR_750_H_

#include <stdint.h>

uint8_t EPD_BWR_750_detect(void);
uint8_t EPD_BWR_750_read_temp(void);
void EPD_BWR_750_init(void);
void EPD_BWR_750_start_dtm1(void);
void EPD_BWR_750_start_dtm2(void);
void EPD_BWR_750_refresh(void);
void EPD_BWR_750_set_sleep(void);

#endif // _EPD_BWR_750_H_
