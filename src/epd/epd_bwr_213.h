#ifndef _EPD_BWR_213_H_
#define _EPD_BWR_213_H_

#include <stdint.h>
#include <stdbool.h>
#include "app_config.h"

uint8_t EPD_BWR_213_detect(void);
uint8_t EPD_BWR_213_read_temp(void);

// Streams dual bit-plane image (BW + Red) directly to UC8151 with on-the-fly style conversion
// style: 0 = Standard, 1 = B&W, 2 = B&W inv, 3 = R&W, 4 = R&W inv
uint8_t EPD_BWR_213_DisplayWithStyle(const uint8_t *bw_image, const uint8_t *red_image, int size, uint8_t style);

#endif // _EPD_BWR_213_H_
