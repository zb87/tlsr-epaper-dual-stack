#ifndef _MODE_SWITCH_H_
#define _MODE_SWITCH_H_

#include <stdint.h>
#include <stdbool.h>
#include "app_config.h"

// Trigger clean transition to specified mode (Zigbee vs BLE)
void mode_switch_to(device_mode_t target_mode);

// Reset Zigbee network associations, set mode to Zigbee, and enter pairing mode
void mode_switch_zigbee_reset(void);

// Software MCU reset
void mcu_soft_reboot(void);

#endif // _MODE_SWITCH_H_
