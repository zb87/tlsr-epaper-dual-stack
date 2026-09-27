#ifndef _ZB_APP_H_
#define _ZB_APP_H_

#include <stdbool.h>
#include "tl_common.h"

void user_zb_init(bool isRetention);
void zb_task(void);
void zb_pm_task(void);
bool zb_is_idle(void);
void zb_start_pairing(void);
bool zb_is_pairing(void);
void zb_start_rejoin(void);
void zb_start_interview_window(void);
bool zb_is_interviewing(void);
void zb_battery_report(void);
void zb_epaper_report_attrs(void);
void zb_power_stats_report(void);
void zb_schedule_mode_switch(uint8_t target_mode);
bool zb_is_mode_switch_pending(void);
void zb_ota_set_active(bool active);
void zb_ota_on_activity(void);
bool zb_is_ota_active(void);
void zb_on_network_leave(void);
void zb_reset_reporting_state(void);
uint16_t zb_get_pan_id(void);
uint8_t zb_get_channel(void);
void zb_send_initial_reports(void);

#endif // _ZB_APP_H_

