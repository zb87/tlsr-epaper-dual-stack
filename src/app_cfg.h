#ifndef _APP_CFG_H_
#define _APP_CFG_H_

#if defined(__cplusplus)
extern "C" {
#endif

#include "version_cfg.h"
#include "app_config.h"
#include "zb_config.h"
#include "stack_cfg.h"

typedef enum {
	EV_POLL_ED_DETECT,
	EV_POLL_PM,
	EV_POLL_HCI,
	EV_POLL_IDLE,
	EV_POLL_MAX,
} ev_poll_e;

#if defined(__cplusplus)
}
#endif

#endif // _APP_CFG_H_
