#include "tl_common.h"
#include "app_config.h"
#include "led.h"

RAM static uint8_t s_led_state = 0;

void led_set(uint8_t mask) {
    s_led_state = mask & LED_ALL;

    // Glitch-free write: set output level first, then ensure output buffer is enabled
    gpio_write(GPIO_LED_RED, (s_led_state & LED_RED) ? 0 : 1);
    gpio_set_func(GPIO_LED_RED, AS_GPIO);
    gpio_set_output_en(GPIO_LED_RED, 1);

    gpio_write(GPIO_LED_GREEN, (s_led_state & LED_GREEN) ? 0 : 1);
    gpio_set_func(GPIO_LED_GREEN, AS_GPIO);
    gpio_set_output_en(GPIO_LED_GREEN, 1);

    // PA7 is shared with SWS: only claim as GPIO output when active
    if (s_led_state & LED_BLUE) {
        gpio_write(GPIO_LED_BLUE, 0);
        gpio_set_func(GPIO_LED_BLUE, AS_GPIO);
        gpio_set_output_en(GPIO_LED_BLUE, 1);
        gpio_set_input_en(GPIO_LED_BLUE, 0);
    } else {
        gpio_write(GPIO_LED_BLUE, 1);
        gpio_set_output_en(GPIO_LED_BLUE, 0);
        gpio_set_func(GPIO_LED_BLUE, AS_SWIRE);
        gpio_set_input_en(GPIO_LED_BLUE, 1);
    }
}

uint8_t led_get_state(void) {
    return s_led_state;
}

void led_init(void) {
    // Red LED: PD2
    gpio_setup_up_down_resistor(GPIO_LED_RED, PM_PIN_PULLUP_1M);
    gpio_write(GPIO_LED_RED, 1); // Active low: 1 = OFF
    gpio_set_func(GPIO_LED_RED, AS_GPIO);
    gpio_set_output_en(GPIO_LED_RED, 1);
    gpio_set_input_en(GPIO_LED_RED, 0);

    // Green LED: PD3 (Zigbee indicator)
    gpio_setup_up_down_resistor(GPIO_LED_GREEN, PM_PIN_PULLUP_1M);
    gpio_write(GPIO_LED_GREEN, 1); // Active low: 1 = OFF
    gpio_set_func(GPIO_LED_GREEN, AS_GPIO);
    gpio_set_output_en(GPIO_LED_GREEN, 1);
    gpio_set_input_en(GPIO_LED_GREEN, 0);

    // Blue LED: PA7 (Shared with SWS debug - default to AS_SWIRE with 1M pull-up)
    gpio_setup_up_down_resistor(GPIO_LED_BLUE, PM_PIN_PULLUP_1M);
    gpio_write(GPIO_LED_BLUE, 1); // Active low: 1 = OFF
    gpio_set_output_en(GPIO_LED_BLUE, 0);
    gpio_set_func(GPIO_LED_BLUE, AS_SWIRE);
    gpio_set_input_en(GPIO_LED_BLUE, 1);

    s_led_state = 0;
}

void led_restore_retention(void) {
    if (s_led_state == 0) {
        return; // PD2/PD3 already configured by gpio_init(0) and 1M pull-ups hold LEDs HIGH (off)
    }
    led_set(s_led_state);
}

void led_on(led_color_t color) {
    led_set(s_led_state | (uint8_t)color);
}

void led_off(led_color_t color) {
    led_set(s_led_state & ~((uint8_t)color));
}

void led_toggle(led_color_t color) {
    led_set(s_led_state ^ (uint8_t)color);
}

void led_blink(led_color_t color, uint32_t duration_ms) {
    uint8_t prev = s_led_state;
    led_on(color);
    WaitMs(duration_ms);
    led_set(prev);
}
