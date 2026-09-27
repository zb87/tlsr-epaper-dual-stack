#ifndef _LED_H_
#define _LED_H_

#include <stdint.h>

typedef enum {
    LED_NONE  = 0x00,
    LED_BLUE  = 0x01, // PA7 (BLE Mode)
    LED_GREEN = 0x02, // PD3 (Zigbee Mode)
    LED_RED   = 0x04, // PD2 (Warning / Error)
    LED_ALL   = 0x07,
} led_color_t;

void led_init(void);
void led_restore_retention(void);
void led_on(led_color_t color);
void led_off(led_color_t color);
void led_toggle(led_color_t color);
void led_set(uint8_t mask);
uint8_t led_get_state(void);
void led_blink(led_color_t color, uint32_t duration_ms);

#endif // _LED_H_
