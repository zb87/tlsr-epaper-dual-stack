#ifndef _ZB_VERSION_H_
#define _ZB_VERSION_H_

#include "version_cfg.h"
#include "app_config.h"

#ifndef ZCL_BASIC_MFG_NAME
#define ZCL_BASIC_MFG_NAME      {6, 'Z','B','-','D','I','Y'}
#endif
#ifndef ZCL_BASIC_MODEL_ID
#if (BOARD == BOARD_HANSHOW_E31PA)
#define ZCL_BASIC_MODEL_ID      {16, 'T','L','S','R','-','X','L','3','N','a','-','E','3','1','P','A'}
#else
#define ZCL_BASIC_MODEL_ID      {15, 'T','L','S','R','-','M','3','N','a','-','E','3','1','H','A'}
#endif
#endif
#ifndef ZCL_BASIC_SW_BUILD_ID
#define ZCL_BASIC_SW_BUILD_ID   {7, 'v', (APP_RELEASE>>4)+0x30, '.', (APP_RELEASE&0xf)+0x30, '.', (APP_BUILD>>4)+0x30, (APP_BUILD&0xf)+0x30, 0}
#endif
#ifndef ZCL_BASIC_DATE_CODE
#define ZCL_BASIC_DATE_CODE     BUILD_DATE
#endif

#endif /* _ZB_VERSION_H_ */
