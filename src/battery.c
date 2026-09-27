#include "tl_common.h"
#include "app_config.h"
#include "battery.h"

extern void adc_channel_init(ADC_InputPchTypeDef p_ain);
extern u16 get_adc_mv(int flg);

RAM static uint16_t s_cached_battery_mv = 0;
RAM static uint32_t s_last_battery_tick = 0;

void battery_init(void) {
    if (s_cached_battery_mv == 0) {
        get_battery_mv_forced();
    }
}

void battery_detect(bool startup_flg) {
    (void)startup_flg;
    get_battery_mv_forced();
}

uint16_t get_battery_mv_forced(void) {
    adc_channel_init(SHL_ADC_VBAT);
    uint16_t mv = get_adc_mv(0);

    if (mv < 1800) mv = 1800;
    if (mv > 3400) mv = 3400;

    s_cached_battery_mv = mv;
    extern u32 pm_get_32k_tick(void);
    s_last_battery_tick = pm_get_32k_tick();
    return mv;
}

uint16_t get_battery_mv(void) {
    extern u32 pm_get_32k_tick(void);
    uint32_t now = pm_get_32k_tick();

    // Re-sample ADC if uninitialized or if >= 10 minutes (600s * 32000 ticks/sec = 19200000 ticks) have elapsed
    if (s_cached_battery_mv == 0 || (uint32_t)(now - s_last_battery_tick) >= (600 * 32000)) {
        return get_battery_mv_forced();
    }
    return s_cached_battery_mv;
}

uint8_t get_battery_level(uint16_t battery_mv) {
    if (battery_mv <= 2200) return 0;
    if (battery_mv >= 3000) return 100;
    return (uint8_t)(((battery_mv - 2200) * 100) / (3000 - 2200));
}
