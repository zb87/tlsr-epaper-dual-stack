#ifndef _BATTERY_H_
#define _BATTERY_H_

#include <stdint.h>

void battery_init(void);
void battery_detect(bool startup_flg);
uint16_t get_battery_mv(void);
uint16_t get_battery_mv_forced(void);
uint8_t get_battery_level(uint16_t battery_mv);

#endif // _BATTERY_H_
