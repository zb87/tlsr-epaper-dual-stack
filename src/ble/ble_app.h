#ifndef _BLE_APP_H_
#define _BLE_APP_H_

#include <stdbool.h>
#include "tl_common.h"

void user_ble_init(bool isRetention);
void ble_task(void);
void ble_pm_task(void);
void ble_display_poll(void);
extern volatile u8 ota_is_working;

#endif // _BLE_APP_H_
