#include "tl_common.h"
#include "app_config.h"
#include "led.h"

RAM static uint8_t s_led_state = 0;

void led_set(uint8_t mask) {
    s_led_state = mask & LED_ALL;
    gpio_write(GPIO_LED_RED,   (s_led_state & LED_RED)   ? 0 : 1);
    gpio_write(GPIO_LED_GREEN, (s_led_state & LED_GREEN) ? 0 : 1);
    gpio_write(GPIO_LED_BLUE,  (s_led_state & LED_BLUE)  ? 0 : 1);
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

    // Blue LED: PA7 (BLE indicator, shared with SWS)
    gpio_setup_up_down_resistor(GPIO_LED_BLUE, PM_PIN_PULLUP_1M);
    gpio_write(GPIO_LED_BLUE, 1); // Active low: 1 = OFF
    gpio_set_func(GPIO_LED_BLUE, AS_GPIO);
    gpio_set_output_en(GPIO_LED_BLUE, 1);
    gpio_set_input_en(GPIO_LED_BLUE, 0);

    s_led_state = 0;
}

void led_restore_retention(void) {
    if (s_led_state == 0) {
        return; // Analog 1M pull-ups already hold LED pins HIGH (off)
    }
    gpio_set_func(GPIO_LED_RED, AS_GPIO);
    gpio_set_output_en(GPIO_LED_RED, 1);
    gpio_set_input_en(GPIO_LED_RED, 0);

    gpio_set_func(GPIO_LED_GREEN, AS_GPIO);
    gpio_set_output_en(GPIO_LED_GREEN, 1);
    gpio_set_input_en(GPIO_LED_GREEN, 0);

    gpio_set_func(GPIO_LED_BLUE, AS_GPIO);
    gpio_set_output_en(GPIO_LED_BLUE, 1);
    gpio_set_input_en(GPIO_LED_BLUE, 0);

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
