/********************************************************************************************************
 * @file    sampleSwitchBLESlave_8258.c
 *
 * @brief   This is the source file for sampleSwitchBLESlave_8258
 *
 * @author  Zigbee Group
 * @date    2021
 *
 * @par     Copyright (c) 2021, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
 *
 *          Licensed under the Apache License, Version 2.0 (the "License");
 *          you may not use this file except in compliance with the License.
 *          You may obtain a copy of the License at
 *
 *              http://www.apache.org/licenses/LICENSE-2.0
 *
 *          Unless required by applicable law or agreed to in writing, software
 *          distributed under the License is distributed on an "AS IS" BASIS,
 *          WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *          See the License for the specific language governing permissions and
 *          limitations under the License.
 *******************************************************************************************************/

#include "tl_common.h"
#include "app_config.h"
#include "stack/ble/ble.h"
#include "ble_cfg.h"
#include "flash.h"
#include "bthome_beacon.h"
#include "battery.h"
#include "flash_eep.h"
#include "mode_switch.h"
#include "epd.h"
#include "epd_slots.h"
#include "epd_prototype.h"
#include "nfc_fm11nc08.h"
#include "ble_app.h"
#include "led.h"
#include "debug_uart.h"
#include "zb_endpoint_cfg.h"
#include "power_tracker.h"

static inline char int_to_hex(u8 num) {
    return (num < 10) ? ('0' + num) : ('A' + num - 10);
}

void zb_ble_ci_cmd_handler(u16 cmd, u8 len, u8 *p) {
    (void)cmd;
    (void)len;
    (void)p;
}

//#include "vendor/common/blt_led.h"
//#include "vendor/common/blt_common.h"

#define RX_FIFO_SIZE	                   64
#define RX_FIFO_NUM		                   8

#define TX_FIFO_SIZE	                   40
#define TX_FIFO_NUM		                   8

typedef struct{
	/** Minimum value for the connection event (interval. 0x0006 - 0x0C80 * 1.25 ms) */
	u16 intervalMin;
	/** Maximum value for the connection event (interval. 0x0006 - 0x0C80 * 1.25 ms) */
	u16 intervalMax;
	/** Number of LL latency connection events (0x0000 - 0x03e8) */
	u16 latency;
	/** Connection Timeout (0x000A - 0x0C80 * 10 ms) */
	u16 timeout;
} gap_periConnectParams_t;

typedef struct _tbl_scanRsp_t {
	u8 size;
	u8 id;
	u8 name[11];
} tbl_scanRsp_t;

u8  mac_public[6];

tbl_scanRsp_t tbl_scanRsp;

/* local function */
static int app_bleOtaRead(void *p);
static int app_bleOtaWrite(void *p);

/* various */
const u16 clientCharacterCfgUUID = GATT_UUID_CLIENT_CHAR_CFG;
const u16 characterPresentFormatUUID = GATT_UUID_CHAR_PRESENT_FORMAT;
const u16 my_primaryServiceUUID = GATT_UUID_PRIMARY_SERVICE;
static const u16 my_characterUUID = GATT_UUID_CHARACTER;
const u16 my_devServiceUUID = SERVICE_UUID_DEVICE_INFORMATION;
const u16 my_PnPUUID = CHARACTERISTIC_UUID_PNP_ID;
const u16 my_devNameUUID = GATT_UUID_DEVICE_NAME;

//device information
const u16 my_gapServiceUUID = SERVICE_UUID_GENERIC_ACCESS;
// Appearance Characteristic Properties
const u16 my_appearanceUIID = 0x2a01;
const u16 my_periConnParamUUID = 0x2a04;
u16 my_appearance = GAP_APPEARE_UNKNOWN;
gap_periConnectParams_t my_periConnParameters = {20, 40, 0, 1000};

#if USE_DEVICE_INFO_CHR_UUID

//#define CHARACTERISTIC_UUID_SYSTEM_ID			0x2A23 // System ID
#define CHARACTERISTIC_UUID_MODEL_NUMBER		0x2A24 // Model Number String: LYWSD03MMC
#define CHARACTERISTIC_UUID_SERIAL_NUMBER		0x2A25 // Serial Number String: F1.0-CFMK-LB-ZCXTJ--
#define CHARACTERISTIC_UUID_FIRMWARE_REV		0x2A26 // Firmware Revision String: 1.0.0_0109
#define CHARACTERISTIC_UUID_HARDWARE_REV		0x2A27 // Hardware Revision String: B1.4
#define CHARACTERISTIC_UUID_SOFTWARE_REV		0x2A28 // Software Revision String: 0x109
#define CHARACTERISTIC_UUID_MANUFACTURER_NAME	0x2A29 // Manufacturer Name String: miaomiaoce.com

//// device Information  attribute values
//static const u16 my_UUID_SYSTEM_ID		    = CHARACTERISTIC_UUID_SYSTEM_ID;
static const u16 my_UUID_MODEL_NUMBER	    = CHARACTERISTIC_UUID_MODEL_NUMBER;
static const u16 my_UUID_SERIAL_NUMBER	    = CHARACTERISTIC_UUID_SERIAL_NUMBER;
static const u16 my_UUID_FIRMWARE_REV	    = CHARACTERISTIC_UUID_FIRMWARE_REV;
static const u16 my_UUID_HARDWARE_REV	    = CHARACTERISTIC_UUID_HARDWARE_REV;
static const u16 my_UUID_SOFTWARE_REV	    = CHARACTERISTIC_UUID_SOFTWARE_REV;
static const u16 my_UUID_MANUFACTURER_NAME  = CHARACTERISTIC_UUID_MANUFACTURER_NAME;
static const u8 my_devNameCharVal[5] = {
	CHAR_PROP_READ | CHAR_PROP_NOTIFY,
	U16_LO(GenericAccess_DeviceName_DP_H), U16_HI(GenericAccess_DeviceName_DP_H),
	U16_LO(GATT_UUID_DEVICE_NAME), U16_HI(GATT_UUID_DEVICE_NAME)
};
static const u8 my_appearanceCharVal[5] = {
	CHAR_PROP_READ,
	U16_LO(GenericAccess_Appearance_DP_H), U16_HI(GenericAccess_Appearance_DP_H),
	U16_LO(GATT_UUID_APPEARANCE), U16_HI(GATT_UUID_APPEARANCE)
};
static const u8 my_periConnParamCharVal[5] = {
	CHAR_PROP_READ,
	U16_LO(CONN_PARAM_DP_H), U16_HI(CONN_PARAM_DP_H),
	U16_LO(GATT_UUID_PERI_CONN_PARAM), U16_HI(GATT_UUID_PERI_CONN_PARAM)
};
static const u8 my_serviceChangeCharVal[5] = {
	CHAR_PROP_INDICATE,
	U16_LO(GenericAttribute_ServiceChanged_DP_H), U16_HI(GenericAttribute_ServiceChanged_DP_H),
	U16_LO(GATT_UUID_SERVICE_CHANGE), U16_HI(GATT_UUID_SERVICE_CHANGE)
};

static const u8 my_ModCharVal[5] = {
	CHAR_PROP_READ,
	U16_LO(DeviceInformation_ModName_DP_H), U16_HI(DeviceInformation_ModName_DP_H),
	U16_LO(CHARACTERISTIC_UUID_MODEL_NUMBER), U16_HI(CHARACTERISTIC_UUID_MODEL_NUMBER)
};
static const u8 my_SerialCharVal[5] = {
	CHAR_PROP_READ,
	U16_LO(DeviceInformation_SerialN_DP_H), U16_HI(DeviceInformation_SerialN_DP_H),
	U16_LO(CHARACTERISTIC_UUID_SERIAL_NUMBER), U16_HI(CHARACTERISTIC_UUID_SERIAL_NUMBER)
};
static const u8 my_FirmCharVal[5] = {
	CHAR_PROP_READ,
	U16_LO(DeviceInformation_FirmRev_DP_H), U16_HI(DeviceInformation_FirmRev_DP_H),
	U16_LO(CHARACTERISTIC_UUID_FIRMWARE_REV), U16_HI(CHARACTERISTIC_UUID_FIRMWARE_REV)
};
static const u8 my_HardCharVal[5] = {
	CHAR_PROP_READ,
	U16_LO(DeviceInformation_HardRev_DP_H), U16_HI(DeviceInformation_HardRev_DP_H),
	U16_LO(CHARACTERISTIC_UUID_HARDWARE_REV), U16_HI(CHARACTERISTIC_UUID_HARDWARE_REV)
};
static const u8 my_SoftCharVal[5] = {
	CHAR_PROP_READ,
	U16_LO(DeviceInformation_SoftRev_DP_H), U16_HI(DeviceInformation_SoftRev_DP_H),
	U16_LO(CHARACTERISTIC_UUID_SOFTWARE_REV), U16_HI(CHARACTERISTIC_UUID_SOFTWARE_REV)
};
static const u8 my_ManCharVal[5] = {
	CHAR_PROP_READ,
	U16_LO(DeviceInformation_ManName_DP_H), U16_HI(DeviceInformation_ManName_DP_H),
	U16_LO(CHARACTERISTIC_UUID_MANUFACTURER_NAME), U16_HI(CHARACTERISTIC_UUID_MANUFACTURER_NAME)
};
static const u8 my_FirmStr[] = {"github.com/pvvx"};
static const u8 my_SoftStr[] = {'Z','0'+(APP_RELEASE>>4),'.','0'+(APP_RELEASE&0x0f),'.','0'+(APP_BUILD>>4),'.','0'+(APP_BUILD&0x0f)}; // "0.1.1.2"
u8 my_HardStr[3];
#if USE_FLASH_SERIAL_UID
u8 my_SerialStr[20]; // "556202-C86013-012345"
#else
static const u8 my_SerialStr[] = {"0001"};
#endif
#if BOARD == BOARD_MJWSD05MMC
static const u8 my_ModelStr[] = {"MJWSD05MMC"};
static const u8 my_ManStr[] = {"miaomiaoce.com"};
#elif BOARD == BOARD_MHO_C401
static const u8 my_ModelStr[] = {"MHO-C401"};
static const u8 my_ManStr[] = {"miaomiaoce.com"};
#elif BOARD == BOARD_MHO_C401N
static const u8 my_ModelStr[] = {"MHO-C401N"};
static const u8 my_ManStr[] = {"miaomiaoce.com"};
#elif BOARD == BOARD_LYWSD03MMC
static const u8 my_ModelStr[] = {"LYWSD03MMC"};
static const u8 my_ManStr[] = {"miaomiaoce.com"};
#elif BOARD == BOARD_CGG1
static const u8 my_ModelStr[] = {"CGG1"};
static const u8 my_ManStr[] = {"Qingping Technology (Beijing) Co., Ltd."};
#elif BOARD == BOARD_CGDK2
static const u8 my_ModelStr[] = {"CGDK2"};
static const u8 my_ManStr[] = {"Qingping Technology (Beijing) Co., Ltd."};
#elif BOARD == BOARD_MHO_C122
static const u8 my_ModelStr[] = {"MHO-C122"};
static const u8 my_ManStr[] = {"MiaoMiaoCe Technology (Beijing) Co., Ltd."};
#elif BOARD == BOARD_TS0201_TZ3000
static const u8 my_ModelStr[] = {"TS0201"};
static const u8 my_ManStr[] = {"Tuya"};
#elif BOARD == BOARD_TH03Z
static const u8 my_ModelStr[] = {"TH03Z"};
static const u8 my_ManStr[] = {"Tuya"};
#elif BOARD == BOARD_HANSHOW_E31PA
static const u8 my_ModelStr[] = {"TLSR-XL3Na-E31PA"};
static const u8 my_ManStr[] = {"ZB-DIY"};
#elif BOARD == BOARD_HANSHOW_E31HA
static const u8 my_ModelStr[] = {"TLSR-M3Na-E31HA"};
static const u8 my_ManStr[] = {"ZB-DIY"};
#else
#error "DEVICE_TYPE = ?"
#endif
//------------------
#endif // USE_DEVICE_INFO_CHR_UUID


const u16 my_gattServiceUUID = SERVICE_UUID_GENERIC_ATTRIBUTE;
const u16 serviceChangeUIID = GATT_UUID_SERVICE_CHANGE;
u16 serviceChangeVal[2] = {0};
static u8 serviceChangeCCC[2]={0,0};


const u8 PROP_READ = CHAR_PROP_READ;
const u8 PROP_WRITE = CHAR_PROP_WRITE;
const u8 PROP_INDICATE = CHAR_PROP_INDICATE;
const u8 PROP_WRITE_NORSP = CHAR_PROP_WRITE_WITHOUT_RSP;
const u8 PROP_READ_NOTIFY = CHAR_PROP_READ | CHAR_PROP_NOTIFY;
const u8 PROP_READ_WRITE_NORSP = CHAR_PROP_READ | CHAR_PROP_WRITE_WITHOUT_RSP;
const u8 PROP_READ_WRITE_WRITENORSP = CHAR_PROP_READ | CHAR_PROP_WRITE | CHAR_PROP_WRITE_WITHOUT_RSP;
const u8 PROP_READ_WRITE = CHAR_PROP_READ | CHAR_PROP_WRITE;
const u8 PROP_READ_WRITE_NORSP_NOTIFY = CHAR_PROP_READ | CHAR_PROP_WRITE_WITHOUT_RSP | CHAR_PROP_NOTIFY;


//////////////////////////////////////////////////////////////////////////////
//	 Adv Packet, Response Packet
//////////////////////////////////////////////////////////////////////////////

/*
 * battery
 * */
const u16 my_batServiceUUID   	= SERVICE_UUID_BATTERY;
const u16 my_batCharUUID        = CHARACTERISTIC_UUID_BATTERY_LEVEL;
static const u8 my_batCharVal[5] = {
	CHAR_PROP_READ | CHAR_PROP_NOTIFY,
	U16_LO(BATT_LEVEL_INPUT_DP_H), U16_HI(BATT_LEVEL_INPUT_DP_H),
	U16_LO(CHARACTERISTIC_UUID_BATTERY_LEVEL), U16_HI(CHARACTERISTIC_UUID_BATTERY_LEVEL)
};
u8 my_batVal = 99;
u16 batteryValueInCCC;


/*
 * ota
 * */
const u8 my_OtaUUID[16]		= TELINK_SPP_DATA_OTA;
const u8 my_OtaServiceUUID[16]		= TELINK_OTA_UUID_SERVICE;
const u16 userdesc_UUID		= GATT_UUID_CHAR_USER_DESC;
const u8  my_OtaName[]      = {'O', 'T', 'A'};
#define TELINK_SPP_DATA_OTA1 0x12,0x2B,0x0d,0x0c,0x0b,0x0a,0x09,0x08,0x07,0x06,0x05,0x04,0x03,0x02,0x01,0x00
static const u8 my_OtaCharVal[19] = {
	CHAR_PROP_READ | CHAR_PROP_WRITE_WITHOUT_RSP,
	U16_LO(OTA_CMD_OUT_DP_H), U16_HI(OTA_CMD_OUT_DP_H),
	TELINK_SPP_DATA_OTA1,
};
typedef struct __attribute__((packed)) {
	u16 confirmed_blk;        // Current confirmed block index
	u8  committed_sectors;    // Number of 4KB sectors written to flash
	u8  error_code;           // 0=OK, 1=Loss, 2=CRC, 3=Write, 4=Incomplete, 5=Timeout
	u8  accepted_chunk_k;     // Configured chunk size in KB (4, 8, or 16)
	u8  max_supported_chunk_k;// Max supported chunk size in KB (16)
} ota_telemetry_t;

ota_telemetry_t s_ota_telemetry;

const u16 my_epdServiceUUID = 0x1314;
const u16 my_epdCmdUUID     = 0x1315;
const u16 my_epdDataUUID    = 0x1316;
const u16 my_epdPwrUUID     = 0x1317;
static const u8 PROP_READ_WRITE_NOTIFY = CHAR_PROP_READ | CHAR_PROP_WRITE | CHAR_PROP_WRITE_WITHOUT_RSP | CHAR_PROP_NOTIFY;
static const u8 my_epdCmdCharVal[5] = {
	CHAR_PROP_READ | CHAR_PROP_WRITE | CHAR_PROP_WRITE_WITHOUT_RSP | CHAR_PROP_NOTIFY,
	U16_LO(EPD_CMD_DP_H), U16_HI(EPD_CMD_DP_H),
	U16_LO(0x1315), U16_HI(0x1315)
};
static const u8 my_epdDataCharVal[5] = {
	CHAR_PROP_READ | CHAR_PROP_WRITE | CHAR_PROP_WRITE_WITHOUT_RSP,
	U16_LO(EPD_DATA_DP_H), U16_HI(EPD_DATA_DP_H),
	U16_LO(0x1316), U16_HI(0x1316)
};
static const u8 my_epdPwrCharVal[5] = {
	CHAR_PROP_READ,
	U16_LO(EPD_PWR_DP_H), U16_HI(EPD_PWR_DP_H),
	U16_LO(0x1317), U16_HI(0x1317)
};
_attribute_custom_bss_ static power_stats_t s_power_stats_val;
static u8  epdCmdVal[23] = {0};
static u16 epdCmdCCC = 0;
static u8  my_epdDataVal = 0;

typedef struct __attribute__((packed)) {
    u16 confirmed_offset; // Confirmed bytes written to RAM (0 to 4000)
    u8  error_code;       // 0=OK, 1=Loss/out-of-order, 2=CRC error
    u8  reserved;
} epd_upload_telemetry_t;

static epd_upload_telemetry_t s_upload_telemetry = {0};

extern void flash_unlock(void);
extern void flash_write_page(unsigned long addr, unsigned long len, unsigned char *buf);
extern void flash_erase_sector(unsigned long addr);

#define OTA_MAX_CHUNK_K          16
#define OTA_MAX_CACHE_SIZE       (OTA_MAX_CHUNK_K * 1024)

extern uint8_t shared_scratch_ram[SHARED_SCRATCH_RAM_SIZE];
#define s_ota_cache              shared_scratch_ram

static uint8_t s_stream_slot = 0;
static uint8_t s_stream_plane = 0;
static uint8_t s_stream_has_red = 0;
static uint8_t s_stream_upload_active = 0;
static uint32_t s_stream_activity_tick = 0;

typedef struct {
    uint8_t active;
    uint8_t slot;
    uint8_t plane;
    uint16_t offset;
} epd_stream_fetch_t;

static epd_stream_fetch_t s_fetch = {0};

static uint8_t s_pending_mode_switch = 0;
static uint32_t s_pending_mode_switch_tick = 0;
static uint8_t s_pending_reboot = 0;
static uint32_t s_pending_reboot_tick = 0;
static uint8_t s_pending_epd_display = 0;
static uint32_t s_pending_epd_display_tick = 0;
static uint8_t s_epd_display_in_progress = 0;

#if DEBUG
static uint8_t  s_log_pull_active = 0;
static uint8_t  s_log_stream_enabled = 0;
static uint16_t s_log_client_seq = 0;
#endif
static uint16_t s_proto_expected_len = 0;

static uint8_t  s_pending_proto_op = 0; // 0=none, 1=commit+exec, 2=re-exec
static uint8_t  s_pending_proto_flags = 0;
static uint32_t s_pending_proto_addr = 0;
static uint8_t  s_proto_running = 0;
static uint8_t  s_proto_result_pending = 0;
static int16_t  s_proto_result_ret = 0;
static uint32_t s_proto_result_dur = 0;

static u16 ota_calc_crc16(const u8 *data, u16 len) {
	u16 crc = 0xFFFF;
	for (u16 i = 0; i < len; i++) {
		crc ^= data[i];
		for (u8 j = 0; j < 8; j++) {
			if (crc & 1) crc = (crc >> 1) ^ 0xA001;
			else crc >>= 1;
		}
	}
	return crc;
}

static int epd_ble_data_read(void *p) {
    (void)p;
    return 0;
}

static void epd_ble_update_status(void) {
    uint16_t vbat = get_battery_mv();
    uint8_t temp = epd_read_temp();
    epdCmdVal[0] = 0x83; // Status response marker
    epdCmdVal[1] = settings.active_slot;
    epdCmdVal[2] = (uint8_t)(vbat >> 8);
    epdCmdVal[3] = (uint8_t)vbat;
    epdCmdVal[4] = temp;
    epdCmdVal[5] = (uint8_t)(settings.screen_refresh_count >> 8);
    epdCmdVal[6] = (uint8_t)settings.screen_refresh_count;
    epdCmdVal[7] = settings.active_mode;
    epdCmdVal[8] = epd_update_state;
    epdCmdVal[9] = settings.render_style;
    epdCmdVal[10] = mcuBootAddrGet();
    epdCmdVal[11] = analog_read(0x3a);
    // Boot console telemetry: Firmware Version (32-bit big-endian)
    epdCmdVal[12] = (uint8_t)(FILE_VERSION >> 24);
    epdCmdVal[13] = (uint8_t)(FILE_VERSION >> 16);
    epdCmdVal[14] = (uint8_t)(FILE_VERSION >> 8);
    epdCmdVal[15] = (uint8_t)FILE_VERSION;
    // Public IEEE MAC address (mac_public[5] .. mac_public[0] order)
    epdCmdVal[16] = mac_public[5];
    epdCmdVal[17] = mac_public[4];
    epdCmdVal[18] = mac_public[3];
    epdCmdVal[19] = mac_public[2];
    epdCmdVal[20] = mac_public[1];
    // Debug firmware indicator (1 = compiled with DEBUG=1, 0 = production DEBUG=0)
#if DEBUG
    epdCmdVal[22] = 1;
#else
    epdCmdVal[22] = 0;
#endif
}

static int epd_ble_cmd_read(void *p) {
    (void)p;
    epd_ble_update_status();
    return 0;
}

static int epd_ble_pwr_read(void *p) {
    (void)p;
    power_tracker_get_stats(&s_power_stats_val);
    return 0;
}

static int epd_ble_data_write(void *p) {
    if (s_epd_display_in_progress) return 0; // Guard shared_scratch_ram while EPD is rendering
    rf_packet_att_data_t *req = (rf_packet_att_data_t *)p;
    u8 len = (req->l2cap >= 3) ? (req->l2cap - 3) : 0;
    u8 *payload = &(req->dat[0]);
    if (len < 3) return 0;
    s_stream_activity_tick = clock_time() | 1;

    if (len == 20) {
        // Pipelined OTA-style 16-byte block with CRC16
        u16 block_idx = payload[0] | (payload[1] << 8);
        u16 calc_crc = ota_calc_crc16(payload, 18);
        u16 pkt_crc = payload[18] | (req->dat[19] << 8);
        if (calc_crc != pkt_crc) {
            s_upload_telemetry.error_code = 2; // CRC error
            return 0;
        }

        u16 exp_blk = s_upload_telemetry.confirmed_offset / 16;
        if (block_idx < exp_blk) {
            // Duplicate retransmission; ignore safely
            return 0;
        }
        if (block_idx > exp_blk) {
            s_upload_telemetry.error_code = 1; // Packet loss / out of order
            return 0;
        }

        uint16_t offset = block_idx * 16;
        if (offset + 16 <= sizeof(s_ota_cache)) {
            memcpy(&s_ota_cache[offset], &payload[2], 16);
            s_upload_telemetry.confirmed_offset = offset + 16;
            s_upload_telemetry.error_code = 0;
        } else {
            s_upload_telemetry.error_code = 3; // Buffer overflow
        }
    } else {
        // Variable chunk format: [off_lo, off_hi, data...]
        uint16_t offset = payload[0] | (payload[1] << 8);
        const uint8_t *chunk_data = &payload[2];
        uint16_t chunk_len = len - 2;
        if (offset + chunk_len <= sizeof(s_ota_cache)) {
            memcpy(&s_ota_cache[offset], chunk_data, chunk_len);
            s_upload_telemetry.confirmed_offset = offset + chunk_len;
            s_upload_telemetry.error_code = 0;
        } else {
            s_upload_telemetry.error_code = 3; // Buffer overflow
        }
    }
    return 0;
}

static int epd_ble_cmd_write(void *p) {
    rf_packet_att_data_t *req = (rf_packet_att_data_t *)p;
    u8 len = (req->l2cap >= 3) ? (req->l2cap - 3) : 0;
    u8 *payload = &(req->dat[0]);
    if (len < 1) return 0;

    // Prevent re-entrant flash / shared_scratch_ram / display mutations during ble_display_poll()
    if (s_epd_display_in_progress &&
        payload[0] != 0x13 && payload[0] != 0x20 &&
        payload[0] != 0x50 && payload[0] != 0x51 && payload[0] != 0x52) {
        return 0;
    }

    switch (payload[0]) {
    case 0x10: // 0x10 <slot> [style]
        if (len >= 2 && payload[1] < EPD_SLOT_COUNT) {
            settings.active_slot = payload[1];
        }
        if (len >= 3 && payload[2] < STYLE_COUNT) {
            settings.render_style = payload[2];
        }
        flash_eep_save();
        s_pending_epd_display = 1;
        s_pending_epd_display_tick = clock_time() | 1;
        bls_pm_setManualLatency(0);
        app_update_nfc_telemetry();
        break;

    case 0x11: // 0x11: Trigger Manual Refresh
        s_pending_epd_display = 1;
        s_pending_epd_display_tick = clock_time() | 1;
        bls_pm_setManualLatency(0);
        break;

    case 0x12: // 0x12 <slot>: Erase Slot
        if (len >= 2) {
            epd_erase_slot(payload[1]);
        }
        break;

    case 0x13: // 0x13: Query Status
        break;

    case 0x14: // 0x14 <slot> <plane>: Prepare Slot Upload
        if (len >= 3) {
            s_fetch.active = 0;
            s_stream_slot = payload[1];
            s_stream_plane = payload[2] ? 1 : 0;
            if (s_stream_plane == 0) s_stream_has_red = 0;
            else s_stream_has_red = 1;
            s_stream_upload_active = 1;
            s_stream_activity_tick = clock_time() | 1;
            s_upload_telemetry.confirmed_offset = 0;
            s_upload_telemetry.error_code = 0;
            epd_prepare_slot_upload(s_stream_slot, s_stream_plane);
            // Ultra-fast connection parameters (10ms interval, 0 latency, matching OTA)
            bls_pm_setManualLatency(0);
            bls_l2cap_requestConnParamUpdate(8, 8, 0, 800);
            return 0; // Quiet ack
        }
        break;

    case 0x15: // 0x15 [auto_display] [has_red]: Commit Slot Upload
        {
            s_fetch.active = 0;
            s_stream_upload_active = 0;
            s_stream_activity_tick = 0;
            uint8_t auto_display = (len >= 2) ? (payload[1] != 0xFF && payload[1] != 0 ? 1 : 0) : 1;
            uint8_t has_red = s_stream_has_red;
            if (len >= 3) has_red = payload[2] ? 1 : 0;

            // Commit RAM buffer to flash in 256-byte page writes
            uint16_t total_bytes = s_upload_telemetry.confirmed_offset;
            if (total_bytes > 0 && total_bytes <= sizeof(s_ota_cache)) {
                uint32_t addr = epd_get_slot_flash_address(s_stream_slot);
                if (addr != 0) {
                    flash_unlock();
                    uint16_t num_pages = (total_bytes + 255) / 256;
                    for (uint16_t p = 0; p < num_pages; p++) {
                        uint16_t page_len = 256;
                        if ((p + 1) * 256 > total_bytes) {
                            page_len = total_bytes - (p * 256);
                        }
                        flash_write_page(addr + (p * 256), page_len, &s_ota_cache[p * 256]);
                    }
                }
            }

            epd_commit_slot_upload(s_stream_slot, has_red, 0);
            if (auto_display) {
                s_pending_epd_display = 1;
                s_pending_epd_display_tick = clock_time() | 1;
                bls_pm_setManualLatency(0);
            } else {
                bls_pm_setManualLatency(19);
            }
            app_update_nfc_telemetry();
            // Restore normal power-saving connection parameters
            bls_l2cap_requestConnParamUpdate(40, 40, 19, 800);
        }
        break;

    case 0x16: // 0x16 <slot> <plane> [<off_lo> <off_hi> <len>]
        if (len >= 3) {
            uint8_t slot = payload[1];
            uint8_t plane = payload[2] ? 1 : 0;

            if (len >= 5) {
                // Backward-compatible single chunk fetch: 0x16 <slot> <plane> <off_lo> <off_hi> [len]
                uint16_t offset = payload[3] | (payload[4] << 8);
                uint8_t req_len = (len >= 6) ? payload[5] : 16;
                if (req_len > 16) req_len = 16;

                uint8_t resp[6 + 16];
                resp[0] = 0x86; // Chunk response marker
                resp[1] = slot;
                resp[2] = plane;
                resp[3] = (uint8_t)(offset & 0xFF);
                resp[4] = (uint8_t)((offset >> 8) & 0xFF);
                uint8_t r_len = epd_read_slot_chunk(slot, plane, offset, &resp[6], req_len);
                resp[5] = r_len;

                bls_att_pushNotifyData(EPD_CMD_DP_H, resp, 6 + r_len);
                return 0;
            } else {
                // Autonomous high-speed stream fetch: 0x16 <slot> <plane>
                s_fetch.active = 1;
                s_fetch.slot = slot;
                s_fetch.plane = plane;
                s_fetch.offset = 0;
                s_stream_activity_tick = clock_time() | 1;
                bls_pm_setManualLatency(0);
                bls_l2cap_requestConnParamUpdate(10, 16, 0, 800);
                return 0;
            }
        }
        break;

    case 0x20: // 0x20 <led_mask> [toggle_flag]: RGB LED Control
        if (len >= 2) {
            uint8_t mask = payload[1];
            if (len >= 3 && payload[2] == 1) {
                led_toggle(mask);
            } else {
                led_set(mask);
            }
        }
        break;

    case 0x30: // 0x30 <mode>: Switch Mode (1 = Zigbee, 2 = BLE)
        if (len >= 2 && payload[1] == DEVICE_MODE_ZIGBEE) {
            DEBUG_LOG("BLE", "CMD_SWITCH_MODE (0x30) to Zigbee received. Acknowledging first before switch...");
            s_pending_mode_switch = DEVICE_MODE_ZIGBEE;
            s_pending_mode_switch_tick = clock_time();
        }
        break;

    case 0x31: // 0x31: Reboot Device
        DEBUG_LOG("BLE", "CMD_REBOOT (0x31) received. Acknowledging first before reboot...");
        s_pending_reboot = 1;
        s_pending_reboot_tick = clock_time();
        break;

    case 0x32: // 0x32: Reset Zigbee Network NVRAM
        DEBUG_LOG("BLE", "CMD_RESET_ZIGBEE (0x32) received. Erasing Zigbee NVRAM (0x30000 - 0x3FFFF)...");
        flash_unlock();
        for (uint32_t addr = NV_BASE_ADDRESS; addr < (NV_BASE_ADDRESS + 0x10000); addr += 0x1000) {
            flash_erase_sector(addr);
        }
        for (int i = 0; i < 3; i++) {
            led_blink(LED_GREEN, 60);
            WaitMs(60);
        }
        DEBUG_LOG("BLE", "Zigbee NVRAM erased successfully.");
        break;

    case 0x40: // 0x40 <len_lo> <len_hi>: Prepare Prototype Snippet Upload
        if (s_proto_running || s_pending_proto_op) return 0;
        s_fetch.active = 0;
        s_stream_upload_active = 2; // Prototype snippet upload
        s_stream_activity_tick = clock_time() | 1;
        s_proto_expected_len = (len >= 3) ? (payload[1] | (payload[2] << 8)) : 0;
        if (s_proto_expected_len > PROTOTYPE_CODE_MAX_SIZE) {
            s_upload_telemetry.error_code = 3; // Buffer overflow
            s_stream_upload_active = 0;
            s_stream_activity_tick = 0;
            return 0;
        }
        memset(s_ota_cache, 0xFF, PROTOTYPE_CODE_MAX_SIZE);
        s_upload_telemetry.confirmed_offset = 0;
        s_upload_telemetry.error_code = 0;
        DEBUG_LOG("BLE", "Prepare prototype upload (%u B expected)", s_proto_expected_len);
        bls_pm_setManualLatency(0);
        bls_l2cap_requestConnParamUpdate(8, 8, 0, 800);
        return 0;

    case 0x41: // 0x41 [flags] [target_addr_b0..b3]: Commit Snippet & Execute
        {
            if (s_proto_running || s_pending_proto_op) return 0;
            s_fetch.active = 0;
            s_stream_upload_active = 0;
            s_stream_activity_tick = 0;
            uint16_t total_bytes = s_upload_telemetry.confirmed_offset;
            if (s_proto_expected_len > 0 && total_bytes < s_proto_expected_len) {
                s_upload_telemetry.error_code = 1; // Incomplete
            }
            if (s_upload_telemetry.error_code != 0 || total_bytes == 0) {
                DEBUG_LOG("BLE", "Proto commit rejected: err=%u, bytes=%u, expected=%u",
                          s_upload_telemetry.error_code, total_bytes, s_proto_expected_len);
                s_proto_result_pending = 1;
                s_proto_result_ret = -1;
                s_proto_result_dur = 0;
                bls_pm_setManualLatency(19);
                bls_l2cap_requestConnParamUpdate(40, 40, 19, 800);
                return 0;
            }

            s_pending_proto_addr = PROTOTYPE_CODE_ADDR;
            s_pending_proto_flags = (len >= 2) ? payload[1] : 0x01; // bit 0: execute immediately
            if (len >= 6) {
                s_pending_proto_addr = (uint32_t)payload[2] | ((uint32_t)payload[3] << 8) |
                                      ((uint32_t)payload[4] << 16) | ((uint32_t)payload[5] << 24);
            }
            s_pending_proto_op = 1; // 1 = commit + exec deferred to ble_task
            return 0;
        }

    case 0x42: // 0x42 [target_addr_b0..b3]: Re-execute existing snippet
        {
            if (s_proto_running || s_pending_proto_op) return 0;
            s_pending_proto_addr = PROTOTYPE_CODE_ADDR;
            if (len >= 5) {
                s_pending_proto_addr = (uint32_t)payload[1] | ((uint32_t)payload[2] << 8) |
                                      ((uint32_t)payload[3] << 16) | ((uint32_t)payload[4] << 24);
            }
            s_pending_proto_flags = 0x01;
            s_pending_proto_op = 2; // 2 = re-exec deferred to ble_task
            return 0;
        }

    case 0x50: // 0x50 <last_seen_seq_lo> <last_seen_seq_hi>: Pull Debug Logs
#if DEBUG
        {
            uint16_t last_seen = 0;
            if (len >= 3) {
                last_seen = payload[1] | (payload[2] << 8);
            }
            if (!s_log_pull_active || last_seen > s_log_client_seq) {
                s_log_client_seq = last_seen;
            }
            s_log_pull_active = 1;
            s_stream_activity_tick = clock_time() | 1;
            return 0;
        }
#else
        return 0;
#endif

    case 0x51: // 0x51 <1=enable, 0=disable>: Toggle Real-time Log Streaming
#if DEBUG
        if (len >= 2) {
            s_log_stream_enabled = payload[1] ? 1 : 0;
            if (s_log_stream_enabled) {
                s_log_client_seq = log_ring_get_latest_seq();
            }
        }
#endif
        return 0;

    case 0x52: // 0x52: Clear Log Ring Buffer
#if DEBUG
        log_ring_clear();
        s_log_client_seq = 0;
        s_log_pull_active = 0;
#endif
        return 0;

    default:
        break;
    }

    epd_ble_update_status();
    bls_att_pushNotifyData(EPD_CMD_DP_H, epdCmdVal, sizeof(epdCmdVal));
    return 0;
}


// TM : to modify
const attribute_t my_Attributes[] = {

	{ATT_END_H - 1, 0,0,0,0,0},	// total num of attribute


	// 0001 - 0007  gap
	{7,ATT_PERMISSIONS_READ,2,2,(u8*)(&my_primaryServiceUUID), 	(u8*)(&my_gapServiceUUID), 0},
		{0,ATT_PERMISSIONS_READ,2,sizeof(my_devNameCharVal),(u8*)(&my_characterUUID), 		(u8*)(my_devNameCharVal), 0},
		{0,ATT_PERMISSIONS_READ,2,sizeof(tbl_scanRsp.name), (u8*)(&my_devNameUUID), (u8*)(tbl_scanRsp.name), 0},
		{0,ATT_PERMISSIONS_READ,2,sizeof(my_appearanceCharVal),(u8*)(&my_characterUUID), 		(u8*)(my_appearanceCharVal), 0},
		{0,ATT_PERMISSIONS_READ,2,sizeof (my_appearance), (u8*)(&my_appearanceUIID), 	(u8*)(&my_appearance), 0},
		{0,ATT_PERMISSIONS_READ,2,sizeof(my_periConnParamCharVal),(u8*)(&my_characterUUID), 		(u8*)(my_periConnParamCharVal), 0},
		{0,ATT_PERMISSIONS_READ,2,sizeof (my_periConnParameters),(u8*)(&my_periConnParamUUID), 	(u8*)(&my_periConnParameters), 0},


	// 0008 - 000b gatt
	{4,ATT_PERMISSIONS_READ,2,2,(u8*)(&my_primaryServiceUUID), 	(u8*)(&my_gattServiceUUID), 0},
		{0,ATT_PERMISSIONS_READ,2,sizeof(my_serviceChangeCharVal),(u8*)(&my_characterUUID), 		(u8*)(my_serviceChangeCharVal), 0},
		{0,ATT_PERMISSIONS_READ,2,sizeof (serviceChangeVal), (u8*)(&serviceChangeUIID), 	(u8*)(&serviceChangeVal), 0},
		{0,ATT_PERMISSIONS_RDWR,2,sizeof (serviceChangeCCC),(u8*)(&clientCharacterCfgUUID), (u8*)(serviceChangeCCC), 0},


#if USE_DEVICE_INFO_CHR_UUID
	// 000c - 0018 Device Information Service
	{13,ATT_PERMISSIONS_READ,2,2,(u8*)(&my_primaryServiceUUID),(u8*)(&my_devServiceUUID), 0},

		{0,ATT_PERMISSIONS_READ,2,sizeof(my_ModCharVal),(u8*)(&my_characterUUID),(u8*)(my_ModCharVal), 0},
		{0,ATT_PERMISSIONS_READ,2,sizeof(my_ModelStr),(u8*)(&my_UUID_MODEL_NUMBER),(u8*)(my_ModelStr), 0},

		{0,ATT_PERMISSIONS_READ,2,sizeof(my_SerialCharVal),(u8*)(&my_characterUUID),(u8*)(my_SerialCharVal), 0},
		{0,ATT_PERMISSIONS_READ,2,sizeof(my_SerialStr),(u8*)(&my_UUID_SERIAL_NUMBER),(u8*)(my_SerialStr), 0},

		{0,ATT_PERMISSIONS_READ,2,sizeof(my_FirmCharVal),(u8*)(&my_characterUUID),(u8*)(my_FirmCharVal), 0},
		{0,ATT_PERMISSIONS_READ,2,sizeof(my_FirmStr),(u8*)(&my_UUID_FIRMWARE_REV),(u8*)(my_FirmStr), 0},

		{0,ATT_PERMISSIONS_READ,2,sizeof(my_HardCharVal),(u8*)(&my_characterUUID),(u8*)(my_HardCharVal), 0},
		{0,ATT_PERMISSIONS_READ,2,sizeof(my_HardStr),(u8*)(&my_UUID_HARDWARE_REV),(u8*)(my_HardStr), 0},

		{0,ATT_PERMISSIONS_READ,2,sizeof(my_SoftCharVal),(u8*)(&my_characterUUID),(u8*)(my_SoftCharVal), 0},
		{0,ATT_PERMISSIONS_READ,2,sizeof(my_SoftStr),(u8*)(&my_UUID_SOFTWARE_REV),(u8*)(my_SoftStr), 0},

		{0,ATT_PERMISSIONS_READ,2,sizeof(my_ManCharVal),(u8*)(&my_characterUUID),(u8*)(my_ManCharVal), 0},
		{0,ATT_PERMISSIONS_READ,2,sizeof(my_ManStr),(u8*)(&my_UUID_MANUFACTURER_NAME),(u8*)(my_ManStr), 0},
#endif

	////////////////////////////////////// Battery Service /////////////////////////////////////////////////////
	// 0019 - 001C
	{4,ATT_PERMISSIONS_READ,2,2,(u8*)(&my_primaryServiceUUID), 	(u8*)(&my_batServiceUUID), 0},
		{0,ATT_PERMISSIONS_READ,2,sizeof(my_batCharVal),(u8*)(&my_characterUUID), 		(u8*)(my_batCharVal), 0},				//prop
		{0,ATT_PERMISSIONS_READ,2,sizeof(my_batVal),(u8*)(&my_batCharUUID), 	(u8*)(&my_batVal), 0},	//value
		{0,ATT_PERMISSIONS_RDWR,2,sizeof(batteryValueInCCC),(u8*)(&clientCharacterCfgUUID), 	(u8*)(&batteryValueInCCC), 0},	//value

	////////////////////////////////////// OTA /////////////////////////////////////////////////////
	// 001D - 0021
	{4,ATT_PERMISSIONS_READ, 2,16,(u8*)(&my_primaryServiceUUID), 	(u8*)(&my_OtaServiceUUID), 0},
		{0,ATT_PERMISSIONS_READ, 2, sizeof(my_OtaCharVal),(u8*)(&my_characterUUID), 		(u8*)(my_OtaCharVal), 0},				//prop
#if USE_BLE_OTA
		{0,ATT_PERMISSIONS_RDWR,16,sizeof(ota_telemetry_t),(u8*)(&my_OtaUUID),	(u8 *)(&s_ota_telemetry), &app_bleOtaWrite, &app_bleOtaRead},
#else
		{0,ATT_PERMISSIONS_RDWR,16,sizeof(my_OtaData),(u8*)(&my_OtaUUID),	(u8 *)(&my_OtaData), &app_bleOtaWrite, &app_bleOtaRead},
#endif
		{0,ATT_PERMISSIONS_READ, 2,sizeof (my_OtaName),(u8*)(&userdesc_UUID), (u8*)(my_OtaName), 0},

	////////////////////////////////////// EPD Display Management Service (0x1314) ////////////////////////////////////
	{8, ATT_PERMISSIONS_READ, 2, 2, (u8*)(&my_primaryServiceUUID), (u8*)(&my_epdServiceUUID), 0},
		{0, ATT_PERMISSIONS_READ, 2, sizeof(my_epdCmdCharVal), (u8*)(&my_characterUUID), (u8*)(my_epdCmdCharVal), 0},
		{0, ATT_PERMISSIONS_RDWR, 2, sizeof(epdCmdVal), (u8*)(&my_epdCmdUUID), (u8*)(epdCmdVal), &epd_ble_cmd_write, &epd_ble_cmd_read},
		{0, ATT_PERMISSIONS_RDWR, 2, sizeof(epdCmdCCC), (u8*)(&clientCharacterCfgUUID), (u8*)(&epdCmdCCC), 0},
		{0, ATT_PERMISSIONS_READ, 2, sizeof(my_epdDataCharVal), (u8*)(&my_characterUUID), (u8*)(my_epdDataCharVal), 0},
		{0, ATT_PERMISSIONS_RDWR, 2, sizeof(s_upload_telemetry), (u8*)(&my_epdDataUUID), (u8*)(&s_upload_telemetry), &epd_ble_data_write, &epd_ble_data_read},
		{0, ATT_PERMISSIONS_READ, 2, sizeof(my_epdPwrCharVal), (u8*)(&my_characterUUID), (u8*)(my_epdPwrCharVal), 0},
		{0, ATT_PERMISSIONS_READ, 2, sizeof(s_power_stats_val), (u8*)(&my_epdPwrUUID), (u8*)(&s_power_stats_val), 0, &epd_ble_pwr_read},

};


_attribute_data_retention_  u8 		 	blt_rxfifo_b[RX_FIFO_SIZE * RX_FIFO_NUM] = {0};
_attribute_data_retention_	my_fifo_t	blt_rxfifo = {
												RX_FIFO_SIZE,
												RX_FIFO_NUM,
												0,
												0,
												blt_rxfifo_b,};


_attribute_data_retention_  u8 		 	blt_txfifo_b[TX_FIFO_SIZE * TX_FIFO_NUM] = {0};
_attribute_data_retention_	my_fifo_t	blt_txfifo = {
												TX_FIFO_SIZE,
												TX_FIFO_NUM,
												0,
												0,
												blt_txfifo_b,};


_attribute_data_retention_	own_addr_type_t 	app_own_address_type = OWN_ADDRESS_PUBLIC;

u8	g_ble_txPowerSet = BLE_DEFAULT_TX_POWER_IDX; // RF_POWER_P3p01dBm;
_attribute_data_retention_	int device_in_connection_state;
//_attribute_data_retention_	u32 advertise_begin_tick;
_attribute_data_retention_	u32	interval_update_tick;
_attribute_data_retention_	u8	sendTerminate_before_enterDeep = 0;
#if (MTU_SIZE_SETTING)
_attribute_data_retention_ 	int  mtuExchange_started_flg = 0;
#endif
volatile bool g_bleConnDoing = 0;
/*
 *  functions
 *
 *
 */
_attribute_ram_code_ int ble_rxfifo_empty(void){
    if(blt_rxfifo.rptr == blt_rxfifo.wptr)    {
        return 1;
    }else {
        return 0;
    }
}

void setAdvTime(u16 count, u16 interval) {
	adv_buf.adv_restore_count = count;
	blta.advInt_min = interval;
	blta.advInt_max = interval + 10;
	blta.adv_interval = interval*625*CLOCK_16M_SYS_TIMER_CLK_1US; // system tick
}

#if USE_BLE_OTA

volatile u8 ota_is_working;
extern void start_reboot(void);

static u32 s_ota_target_addr;
static u32 s_ota_written_bytes;
static u16 s_ota_chunk_blocks;
static u16 s_ota_buffered_blocks;
static u16 s_ota_expected_blk;
static u16 s_last_ota_cmd = 0;
static u32 s_ota_last_erased_sector;

extern u32 blt_ota_start_tick;

static void ota_flush_cache_to_flash(void) {
	if (s_ota_buffered_blocks == 0) return;

	u32 flash_addr = s_ota_target_addr + s_ota_written_bytes;
	u16 total_bytes = s_ota_buffered_blocks * 16;
	u16 num_pages = (total_bytes + 255) / 256;

	flash_unlock();

	// Erase any 4KB sectors that this flush will write into
	u32 sec_start = flash_addr & ~4095;
	u32 sec_end = (flash_addr + total_bytes - 1) & ~4095;
	for (u32 sec = sec_start; sec <= sec_end; sec += 4096) {
		if (s_ota_last_erased_sector == 0xFFFFFFFF || sec > s_ota_last_erased_sector) {
			flash_erase_sector(sec);
			s_ota_last_erased_sector = sec;
		}
	}

	if (s_ota_written_bytes == 0) {
		// Sector 0 safety: protect boot signature at offset 8 by writing 0xFF
		u8 original_byte8 = s_ota_cache[8];
		s_ota_cache[8] = 0xFF;
		for (u16 p = 0; p < num_pages; p++) {
			u16 page_len = 256;
			if ((p + 1) * 256 > total_bytes) {
				page_len = total_bytes - (p * 256);
			}
			flash_write_page(flash_addr + (p * 256), page_len, &s_ota_cache[p * 256]);
		}
		s_ota_cache[8] = original_byte8;
	} else {
		for (u16 p = 0; p < num_pages; p++) {
			u16 page_len = 256;
			if ((p + 1) * 256 > total_bytes) {
				page_len = total_bytes - (p * 256);
			}
			flash_write_page(flash_addr + (p * 256), page_len, &s_ota_cache[p * 256]);
		}
	}

	s_ota_written_bytes += total_bytes;
	s_ota_telemetry.committed_sectors = (u8)(s_ota_written_bytes / 4096);
	s_ota_buffered_blocks = 0;
	DEBUG_LOG("OTA", "Flush: %u B at 0x%05X (%u secs)", total_bytes, (unsigned int)flash_addr, s_ota_telemetry.committed_sectors);
}

static int app_bleOtaWrite(void * p) {
	if (s_epd_display_in_progress) return 0;
	blt_ota_start_tick = clock_time() | 1;
	rf_packet_att_data_t *req = (rf_packet_att_data_t *)p;
	u16 cmd_type = req->dat[0] | (req->dat[1] << 8);
	s_last_ota_cmd = cmd_type;

	if (cmd_type == 0xFF01) { // CMD_OTA_START
		ota_is_working = 1;
		flash_unlock();
		bls_pm_setManualLatency(0);
		bls_ota_setTimeout(60 * 1000000);

		// Request ultra-fast connection interval for OTA (10ms interval, 0 latency)
		bls_l2cap_requestConnParamUpdate(8, 8, 0, 800);

		mcuBootAddr = mcuBootAddrGet();
		s_ota_target_addr = mcuBootAddr ? 0 : FLASH_ADDR_OF_OTA_IMAGE;
		ota_firmware_size_k = BANK1_OTA_MAX_SIZE / 1024;
		ota_program_offset = s_ota_target_addr;

		// Parse requested chunk size (in KB): 4, 8, or 16
		// req->l2cap: ATT payload is (l2cap - 3). If >= 3 bytes, dat[2] is present.
		u8 req_chunk_k = 4;
		u8 dat_len = (req->l2cap > 3) ? (req->l2cap - 3) : 0;
		if (dat_len >= 3 && req->dat[2] > 0) {
			req_chunk_k = req->dat[2];
		}
		if (req_chunk_k >= 16) {
			req_chunk_k = 16;
		} else if (req_chunk_k >= 8) {
			req_chunk_k = 8;
		} else {
			req_chunk_k = 4;
		}

		s_ota_chunk_blocks = req_chunk_k * 64; // 1 KB = 64 blocks of 16 bytes
		s_ota_written_bytes = 0;
		s_ota_buffered_blocks = 0;
		s_ota_expected_blk = 0;
		s_ota_last_erased_sector = 0xFFFFFFFF;

		memset(&s_ota_telemetry, 0, sizeof(s_ota_telemetry));
		s_ota_telemetry.accepted_chunk_k = req_chunk_k;
		s_ota_telemetry.max_supported_chunk_k = OTA_MAX_CHUNK_K;

		analog_write(0x3a, 0);
		analog_write(0x3b, 0);
		analog_write(0x38, 0);

		DEBUG_LOG("OTA", "CMD_OTA_START: Target 0x%05X (B%d), Chunk %u KB (%u blks)",
		          (unsigned int)s_ota_target_addr, s_ota_target_addr ? 1 : 0,
		          req_chunk_k, s_ota_chunk_blocks);
		return 0;
	} else if (cmd_type == 0xFF02) { // CMD_OTA_END
		flash_unlock();
		// Flush any remaining buffered blocks
		ota_flush_cache_to_flash();

		u16 last_idx = req->dat[2] | (req->dat[3] << 8);
		u16 inv_idx  = req->dat[4] | (req->dat[5] << 8);

		if ((last_idx ^ inv_idx) == 0xFFFF && last_idx == (s_ota_expected_blk - 1)) {
			DEBUG_LOG("OTA", "CMD_OTA_END: Verified %u blocks (%u bytes). Committing boot flag...",
			          s_ota_expected_blk, (unsigned int)s_ota_written_bytes);

			// Write 0x4B to new bank offset 8
			u8 flag_4b = 0x4B;
			u32 zero = 0;
			flash_write_page(s_ota_target_addr + 8, 1, &flag_4b);
			// Invalidate old bank offset 8
			u32 old_boot_addr = s_ota_target_addr ? 0 : FLASH_ADDR_OF_OTA_IMAGE;
			flash_write_page(old_boot_addr + 8, 4, (u8 *)&zero);

			s_ota_telemetry.error_code = 0;
			DEBUG_LOG("OTA", "Boot flag set! Soft rebooting into Bank %d...", s_ota_target_addr ? 1 : 0);
			analog_write(0x3a, 0);
			analog_write(0x3b, 0);
			analog_write(0x38, 0);
			start_reboot();
		} else {
			s_ota_telemetry.error_code = 4; // OTA_RESULT_INCOMPLETE
			analog_write(0x3a, 4);
			analog_write(0x3b, (u8)(s_ota_expected_blk & 0xFF));
			analog_write(0x38, (u8)((s_ota_expected_blk >> 8) & 0xFF));
			DEBUG_LOG("OTA", "CMD_OTA_END FAILED: last_idx=%u, exp=%u, inv=0x%04X",
			          last_idx, s_ota_expected_blk - 1, inv_idx);
		}
		return 0;
	} else if (cmd_type < 0xFF00) {
		// Check CRC16 of block
		u16 calc_crc = ota_calc_crc16(req->dat, 18);
		u16 pkt_crc = req->dat[18] | (req->dat[19] << 8);
		if (calc_crc != pkt_crc) {
			s_ota_telemetry.error_code = 2; // OTA_RESULT_CRC_ERR
			analog_write(0x3a, 2);
			analog_write(0x3b, (u8)(cmd_type & 0xFF));
			analog_write(0x38, (u8)((cmd_type >> 8) & 0xFF));
			DEBUG_LOG("OTA", "CRC error at block %u (calc 0x%04X != pkt 0x%04X)",
			          cmd_type, calc_crc, pkt_crc);
			return 0;
		}

		// Check sequence
		if (cmd_type < s_ota_expected_blk) {
			// Duplicate retransmission; ignore safely
			return 0;
		}
		if (cmd_type > s_ota_expected_blk) {
			s_ota_telemetry.error_code = 1; // OTA_RESULT_PACKET_LOSS
			analog_write(0x3a, 1);
			analog_write(0x3b, (u8)(cmd_type & 0xFF));
			analog_write(0x38, (u8)((cmd_type >> 8) & 0xFF));
			DEBUG_LOG("OTA", "Packet loss: expected %u, got %u", s_ota_expected_blk, cmd_type);
			return 0;
		}

		// Cache 16-byte block into RAM buffer (zero flash latency!)
		u16 cache_offset = s_ota_buffered_blocks * 16;
		if (cache_offset + 16 <= OTA_MAX_CACHE_SIZE) {
			memcpy(&s_ota_cache[cache_offset], &req->dat[2], 16);
			s_ota_buffered_blocks++;
			s_ota_expected_blk++;
			s_ota_telemetry.confirmed_blk = s_ota_expected_blk;
		}

		// Periodic lightweight progress log every 1024 blocks (16 KB)
		if ((s_ota_expected_blk % 1024) == 0) {
			DEBUG_LOG("OTA", "RAM buffered: %u blocks (%u KB)",
			          s_ota_expected_blk, (unsigned int)(s_ota_expected_blk * 16 / 1024));
		}
		return 0;
	}
	return 0;
}

static int app_bleOtaRead(void *p) {
	// If full configured chunk is buffered in RAM, flush to flash now.
	// The central is waiting for ATT_READ_RSP, providing natural backpressure
	// and guaranteeing that no RF packets are transmitted while interrupts are disabled.
	if (s_ota_buffered_blocks >= s_ota_chunk_blocks) {
		ota_flush_cache_to_flash();
	}
	s_ota_telemetry.confirmed_blk = s_ota_expected_blk;
	return 0;
}
#else
static int app_bleOtaWrite(void *p){
	rf_packet_att_data_t *req = (rf_packet_att_data_t*)p;
	u8 len = req->rf_len - 9;
	u16 cmd_type =  req->dat[0] ;
	cmd_type <<= 8;
	cmd_type |= req->dat[1] ;
	zb_ble_ci_cmd_handler(cmd_type, len, &(req->dat[2]));
	return 0;
}
static int app_bleOtaRead(void *p){
	return 0;
}
#endif


unsigned char * str_bin2hex(unsigned char *d, unsigned char *s, int len) {
	static const char* hex_ascii = { "0123456789ABCDEF" };
	while(len--) {
		*d++ = hex_ascii[(*s >> 4) & 0xf];
		*d++ = hex_ascii[(*s++ >> 0) & 0xf];
	}
	return d;
}

static void my_att_init(void){
#if USE_FLASH_SERIAL_UID
	u8 buf[16];
	u32 mid;
	u8 *p = my_SerialStr;
	// Read SoC ID, version
	buf[0] = REG_ADDR8(0x7f);
	buf[1] = REG_ADDR8(0x7e);
	buf[2] = REG_ADDR8(0x7d);
	p = str_bin2hex(p, buf, 3);
	*p++ = '-';
	memset(buf, 0, sizeof(buf));
	// Read flash ID and UID
	flash_read_mid_uid_with_check(&mid, buf);
	p = str_bin2hex(p, (unsigned char *)&mid, 3);
	*p++ = '-';
	memcpy(p, buf, 6);
	//ser_uid_txt(p, &buf[4], 7);
#endif

	my_HardStr[0] = 'V';
    str_bin2hex(&my_HardStr[1], &g_zcl_basicAttrs.hwVersion, 1);

    bls_att_setAttributeTable ((u8 *)my_Attributes);
}


void app_switch_to_indirect_adv(u8 e, u8 *p, int n){

	bls_ll_setAdvParam( DEF_ADV_INTERVAL_MIN, DEF_ADV_INTERVAL_MAX,
						ADV_TYPE_CONNECTABLE_UNDIRECTED, OWN_ADDRESS_PUBLIC,
						0,  NULL,
						DEF_APP_ADV_CHANNEL,
						ADV_FP_NONE);

	bls_ll_setAdvEnable(BLC_ADV_ENABLE);  //must: set adv enable
	DEBUG_LOG("BLE", "Advertising started: %s (interval %ums)", (char*)tbl_scanRsp.name, (DEF_ADV_INTERVAL_MIN * 625) / 1000);
}



void 	ble_remote_terminate(u8 e,u8 *p, int n){ //*p is terminate reason

	device_in_connection_state = 0;
#if USE_BLE_OTA
	ota_is_working = 0;
#endif
	s_fetch.active = 0;
	s_stream_upload_active = 0;
	s_stream_activity_tick = 0;
#if DEBUG
	s_log_pull_active = 0;
	s_log_stream_enabled = 0;
#endif
	s_pending_proto_op = 0;
	s_proto_running = 0;
	s_proto_result_pending = 0;
	s_pending_epd_display = 0;
	s_epd_display_in_progress = 0;
	bls_pm_setManualLatency(19);
	u8 reason = *p;
	const char *reason_str = "OTHER";
	if (reason == HCI_ERR_CONN_TIMEOUT) reason_str = "HCI_ERR_CONN_TIMEOUT (0x08)";
	else if (reason == HCI_ERR_REMOTE_USER_TERM_CONN) reason_str = "HCI_ERR_REMOTE_USER_TERM_CONN (0x13)";
	else if (reason == HCI_ERR_CONN_TERM_MIC_FAILURE) reason_str = "HCI_ERR_CONN_TERM_MIC_FAILURE (0x3D)";
	else if (reason == HCI_ERR_CONN_TERM_BY_LOCAL_HOST) reason_str = "HCI_ERR_CONN_TERM_BY_LOCAL_HOST (0x16)";
	else if (reason == 0x3B) reason_str = "UNACCEPTABLE_CONN_INTERVAL (0x3B)";
	else if (reason == 0x22) reason_str = "LL_RESPONSE_TIMEOUT (0x22)";
	else if (reason == 0x28) reason_str = "INSTANT_PASSED (0x28)";
	DEBUG_LOG("BLE", "Connection Terminated! Code: 0x%02X (%s)", reason, reason_str);

#if (MTU_SIZE_SETTING)
	mtuExchange_started_flg = 0;
#endif

#if (1 || BLE_APP_PM_ENABLE)
	 //user has push terminate pkt to ble TX buffer before deepsleep
	if(sendTerminate_before_enterDeep == 1){
		sendTerminate_before_enterDeep = 2;
	}
#endif

	bls_ll_setAdvEnable(BLC_ADV_DISABLE);  //adv disable
	adv_buf.adv_restore_count = 1;
	if(*p != HCI_ERR_OP_CANCELLED_BY_HOST){
		bls_ll_setAdvEnable(BLC_ADV_ENABLE);  //adv enable
	}
}

#if 0
void 	ble_exception_data_abandom(u8 e,u8 *p, int n){
	T_bleDataAbandom++;
}
#endif

static u8 s_suspend_irq_saved = 0;

_attribute_ram_code_
void	user_set_rf_power (u8 e, u8 *p, int n){
	rf_set_power_level_index(g_ble_txPowerSet);
	if (epd_update_state == 1) {
		gpio_set_func(GPIO_EPD_BUSY, AS_GPIO);
		gpio_set_output_en(GPIO_EPD_BUSY, 0);
		gpio_set_input_en(GPIO_EPD_BUSY, 1);
#if (BOARD == BOARD_HANSHOW_E31PA)
		gpio_setup_up_down_resistor(GPIO_EPD_BUSY, PM_PIN_UP_DOWN_FLOAT);
#else
		gpio_setup_up_down_resistor(GPIO_EPD_BUSY, PM_PIN_PULLUP_1M);
#endif
	}
	power_tracker_on_wake();
	if (s_suspend_irq_saved) {
		s_suspend_irq_saved = 0;
		// Guard against hardware equality comparator miss if 32kHz RC jitter
		// reached blt_next_event_tick while IRQs were masked across rf_ble_1m_param_init().
		if (blc_ll_getCurrentState() == BLS_LINK_STATE_CONN &&
		    !(reg_irq_src & FLD_IRQ_SYSTEM_TIMER)) {
			u32 now = clock_time();
			if ((u32)(now + 160 - reg_system_tick_irq) < BIT(30)) {
				reg_system_tick_irq = now + 160;
			}
		}
		irq_enable();
	}
}



void task_connect (u8 e, u8 *p, int n){

	bls_l2cap_requestConnParamUpdate (DEF_CON_PAR_UPDATE);

	device_in_connection_state = 1;//
	s_pending_epd_display = 0;
	s_epd_display_in_progress = 0;

	interval_update_tick = clock_time() | 1; //none zero
	DEBUG_LOG("BLE", "Connected to BLE Central host! (e=0x%02X)", e);
}


void	task_conn_update_req (u8 e, u8 *p, int n){
	DEBUG_LOG("BLE", "L2CAP ConnParam update requested");
}

void	task_conn_update_done (u8 e, u8 *p, int n){
	DEBUG_LOG("BLE", "L2CAP ConnParam update complete");
}

void blc_initMacAddress(int flash_addr, u8 *mac_public, u8 *mac_random_static){
	u8 mac_read[8];
	u8 value_rand[5];
	flash_read_page(flash_addr, 8, mac_read);
	u8 ff_six_byte[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	generateRandomNum(sizeof(value_rand), value_rand);

	if ( memcmp(mac_read, ff_six_byte, sizeof(mac_read)) ) {
		memcpy(mac_public, mac_read, 6);  //copy public address from flash
	}
	else {  //no public address on flash
		mac_public[0] = value_rand[0];
		mac_public[1] = value_rand[1];
		mac_public[2] = value_rand[2];
		mac_public[3] = 0x38;             //company id: 0xA4C138
		mac_public[4] = 0xC1;
		mac_public[5] = 0xA4;
		mac_public[6] = value_rand[3];
		mac_public[7] = value_rand[4];

		flash_write_page (flash_addr, 8, mac_public);
	}

	mac_random_static[0] = mac_public[0];
	mac_random_static[1] = mac_public[1];
	mac_random_static[2] = mac_public[2];
	mac_random_static[3] = value_rand[3];
	mac_random_static[4] = value_rand[4];
	mac_random_static[5] = 0xC0; 			//for random static
}

bool ble_connection_doing(void){
	return g_bleConnDoing;
}

int app_host_event_callback (u32 h, u8 *para, int n){
	u8 event = h & 0xFF;

	switch(event)
	{
		case GAP_EVT_SMP_PARING_BEAGIN:
		{
			g_bleConnDoing = 1;
		}
		break;

		case GAP_EVT_SMP_PARING_SUCCESS:
		{
			g_bleConnDoing = 0;
		}
		break;

		case GAP_EVT_SMP_PARING_FAIL:
		{
			g_bleConnDoing = 0;
		}
		break;

		case GAP_EVT_SMP_CONN_ENCRYPTION_DONE:
		{
#if (MTU_SIZE_SETTING)
			if(!mtuExchange_started_flg){  //master do not send MTU exchange request in time
				blc_att_requestMtuSizeExchange(BLS_CONN_HANDLE, MTU_SIZE_SETTING);
			}
#endif
		}
		break;

		case GAP_EVT_ATT_EXCHANGE_MTU:
		{
#if (MTU_SIZE_SETTING)
//			gap_gatt_mtuSizeExchangeEvt_t *pEvt = (gap_gatt_mtuSizeExchangeEvt_t *)para;
			mtuExchange_started_flg = 1;   //set MTU size exchange flag here
#endif
		}
		break;


		default:
		break;
	}

	return 0;
}


void bls_set_advertise_prepare(void *p);

_attribute_ram_code_
static void suspend_enter_callback(u8 e, u8 *p, int n) {
    (void)e; (void)p; (void)n;
    debug_uart_flush();
    power_tracker_on_sleep();
    cpu_set_gpio_wakeup(GPIO_NFC_IRQ, Level_Low, 1);
    if (epd_is_busy() && gpio_read(GPIO_EPD_BUSY) == 0) {
        cpu_set_gpio_wakeup(GPIO_EPD_BUSY, Level_High, 1);
    } else {
        cpu_set_gpio_wakeup(GPIO_EPD_BUSY, Level_High, 0);
    }
    bls_pm_setWakeupSource(PM_WAKEUP_PAD | PM_WAKEUP_TIMER);
}

#if(BLE_APP_PM_ENABLE)
static u8 app_ble_pm_condition(void) {
#if USE_BLE_OTA
	if (ota_is_working) return 0;
#endif
#if DEBUG
	if (s_fetch.active || s_stream_upload_active || s_log_pull_active || s_log_stream_enabled ||
#else
	if (s_fetch.active || s_stream_upload_active ||
#endif
	    s_pending_proto_op || s_proto_running || s_proto_result_pending ||
	    s_pending_mode_switch || s_pending_reboot ||
	    s_pending_epd_display || s_epd_display_in_progress) {
		return 0;
	}
	if (nfc_fm11nc08_is_active() || !gpio_read(GPIO_NFC_IRQ)) {
		return 0;
	}
	return 1;
}
#endif

void user_ble_normal_init(void){

	 //blc_app_loadCustomizedParameters();  //load customized freq_offset cap value

////////////////// BLE stack initialization ////////////////////////////////////
	u8  mac_random_static[6];
	blc_initMacAddress(CFG_MAC_ADDRESS, mac_public, mac_random_static);

#if(BLE_DEVICE_ADDRESS_TYPE == BLE_DEVICE_ADDRESS_RANDOM_STATIC)
	blc_ll_setRandomAddr(mac_random_static);
#endif

	////// Controller Initialization  //////////
	blc_ll_initBasicMCU();                      //mandatory
	blc_ll_initStandby_module(mac_public);				//mandatory
	blc_ll_initAdvertising_module(mac_public); 	//adv module: 		 mandatory for BLE slave,
	blc_ll_initConnection_module();				//connection module: mandatory for BLE connectable,
	blc_ll_initSlaveRole_module();				//slave module: 	 mandatory for BLE slave,
	blc_ll_initPowerManagement_module();        //pm module:      	 optional

	////// Host Initialization  //////////
	blc_gap_peripheral_init();    //gap initialization
	my_att_init(); //gatt initialization
	blc_l2cap_register_handler (blc_l2cap_packet_receive);  	//l2cap initialization

	//Smp Initialization may involve flash write/erase(when one sector stores too much information,
	//   is about to exceed the sector threshold, this sector must be erased, and all useful information
	//   should re_stored) , so it must be done after battery check
#if (APP_SECURITY_ENABLE)
	blc_smp_peripheral_init();
#else
	blc_smp_setSecurityLevel(No_Security);
#endif

	blc_gap_registerHostEventHandler( app_host_event_callback );
	blc_gap_setEventMask( GAP_EVT_MASK_SMP_PARING_BEAGIN 			|  \
						  GAP_EVT_MASK_SMP_PARING_SUCCESS   		|  \
						  GAP_EVT_MASK_SMP_PARING_FAIL				|  \
						  GAP_EVT_MASK_SMP_CONN_ENCRYPTION_DONE 	|  \
						  GAP_EVT_MASK_ATT_EXCHANGE_MTU);



///////////////////// USER application initialization ///////////////////


	////////////////// config adv packet /////////////////////

	bls_set_advertise_prepare(app_advertise_prepare_handler); // TODO: not work if EXTENDED_ADVERTISING
	app_advertise_prepare_handler(NULL);
//	bls_ll_setAdvData( (u8 *)tbl_advData, sizeof(tbl_advData) );
	tbl_scanRsp.size = sizeof(tbl_scanRsp.name) + sizeof(tbl_scanRsp.id) ;
	tbl_scanRsp.id = GAP_ADTYPE_LOCAL_NAME_COMPLETE;
	tbl_scanRsp.name[0]  = 'T';
	tbl_scanRsp.name[1]  = 'L';
	tbl_scanRsp.name[2]  = 'S';
	tbl_scanRsp.name[3]  = 'R';
	tbl_scanRsp.name[4]  = '-';
	tbl_scanRsp.name[5]  = int_to_hex(mac_public[2] >> 4);
	tbl_scanRsp.name[6]  = int_to_hex(mac_public[2] & 0x0f);
	tbl_scanRsp.name[7]  = int_to_hex(mac_public[1] >> 4);
	tbl_scanRsp.name[8]  = int_to_hex(mac_public[1] & 0x0f);
	tbl_scanRsp.name[9]  = int_to_hex(mac_public[0] >> 4);
	tbl_scanRsp.name[10] = int_to_hex(mac_public[0] & 0x0f);
	bls_ll_setAdvEnable(BLC_ADV_DISABLE);
	bls_ll_setScanRspData((u8 *)&tbl_scanRsp, sizeof(tbl_scanRsp));
	app_switch_to_indirect_adv(0,0,0); //adv enable


	//set rf power index, user must set it after every suspend wakeup, cause relative setting will be reset in suspend
	user_set_rf_power(0, 0, 0);
	bls_app_registerEventCallback (BLT_EV_FLAG_SUSPEND_EXIT, &user_set_rf_power);
	bls_app_registerEventCallback (BLT_EV_FLAG_SUSPEND_ENTER, &suspend_enter_callback);

	//ble event call back
	bls_app_registerEventCallback (BLT_EV_FLAG_CONNECT, &task_connect);
	bls_app_registerEventCallback (BLT_EV_FLAG_TERMINATE, &ble_remote_terminate);

	bls_app_registerEventCallback (BLT_EV_FLAG_CONN_PARA_REQ, &task_conn_update_req);
	bls_app_registerEventCallback (BLT_EV_FLAG_CONN_PARA_UPDATE, &task_conn_update_done);

#if USE_BLE_OTA
extern u8 mcuBootAddr; //boot address flag
extern u8 mcuBootAddrGet(void);
	mcuBootAddr = mcuBootAddrGet();
	ota_firmware_size_k = BANK1_OTA_MAX_SIZE / 1024;
	u32 target_boot_addr = mcuBootAddr ? 0 : FLASH_ADDR_OF_OTA_IMAGE;
	ota_program_offset = target_boot_addr;
	bls_ota_set_fwSize_and_fwBootAddr(ota_firmware_size_k, target_boot_addr);
	bls_ota_setTimeout(60 * 1000000);
#endif

	///////////////////// Power Management initialization///////////////////
#if(BLE_APP_PM_ENABLE)
	blc_pm_setDeepsleepRetentionThreshold(95, 95);
	blc_pm_setDeepsleepRetentionEarlyWakeupTiming(400);
	blc_pm_setDeepsleepRetentionType(DEEPSLEEP_MODE_RET_SRAM_LOW32K);
	blc_att_setRxMtuSize(250);
	bls_pm_conditionCbRegister(app_ble_pm_condition);
	bls_pm_setSuspendMask(SUSPEND_DISABLE);
#else
	bls_pm_setSuspendMask (SUSPEND_DISABLE);
#endif
//	advertise_begin_tick = clock_time();
}

// Binary-accurate offsets of bltPm in SDK/stack/ble/lib/libble_8258.a(ll_pm.o).
// Note: SDK/stack/ble/ble_8258/ll/ll_pm.h declares an extra 'u16 latency_en' field
// not present in libble_8258.a, shifting fields from sys_latency onward by -4 bytes.
// Direct word/byte pointer access is used because global -fpack-struct would otherwise
// force unaligned byte-by-byte loads on struct members.
#define BLTPM_B_SUSPEND_MASK            0   // u8  suspend_mask
#define BLTPM_B_DEEPRT_EN               28  // u8  deepRt_en
#define BLTPM_B_DEEPRET_TYPE            29  // u8  deepRet_type
#define BLTPM_W_DEEPRET_ADV_THRES       4   // u32 deepRet_advThresTick (offset 16)
#define BLTPM_W_DEEPRET_CONN_THRES      5   // u32 deepRet_connThresTick (offset 20)
#define BLTPM_W_DEEPRET_EARLY_WAKEUP    6   // u32 deepRet_earlyWakeupTick (offset 24)

// In the Dual-Mode SDK libble_8258.a(ll_pm.o), blt_brx_sleep() was compiled with
// the deep-retention upgrade check omitted (hardcoding SUSPEND_MODE = 0), even though
// blc_ll_recoverDeepRetention() and blc_pm_setDeepsleepRetention*() are fully present.
// This wrapper restores the exact threshold & early-wakeup upgrade from liblt_8258.a.
// During an active EPD refresh (epd_is_busy()), it keeps SUSPEND_MODE so digital GPIO
// output drivers (EPD power P-MOSFET, RESET, CS, and status LEDs) stay continuously latched.
_attribute_ram_code_
static int ble_cpu_sleep_wakeup_32k_rc(SleepMode_TypeDef sleep_mode,
                                       SleepWakeupSrc_TypeDef wakeup_src,
                                       unsigned int wakeup_tick) {
	if (sleep_mode == SUSPEND_MODE && (wakeup_src & PM_WAKEUP_TIMER) && !epd_is_busy()) {
		volatile u8  *pm8  = (volatile u8 *)&bltPm;
		volatile u32 *pm32 = (volatile u32 *)&bltPm;
		u8 state = blc_ll_getCurrentState();
		u8 mask  = pm8[BLTPM_B_SUSPEND_MASK];
		u32 thres_tick = 0;
		bool deep_ret_allow = false;

		if (state == BLS_LINK_STATE_ADV && (mask & DEEPSLEEP_RETENTION_ADV)) {
			thres_tick = pm32[BLTPM_W_DEEPRET_ADV_THRES];
			deep_ret_allow = true;
		} else if (state == BLS_LINK_STATE_CONN && (mask & DEEPSLEEP_RETENTION_CONN)) {
			thres_tick = pm32[BLTPM_W_DEEPRET_CONN_THRES];
			deep_ret_allow = true;
		}

		if (deep_ret_allow && ((u32)(wakeup_tick - clock_time() - thres_tick) < BIT(30))) {
			pm8[BLTPM_B_DEEPRT_EN] = 1;
			wakeup_tick -= pm32[BLTPM_W_DEEPRET_EARLY_WAKEUP];
			sleep_mode = (SleepMode_TypeDef)pm8[BLTPM_B_DEEPRET_TYPE];
		}
	}

	if (sleep_mode == SUSPEND_MODE && (wakeup_src & PM_WAKEUP_TIMER) &&
	    blc_ll_getCurrentState() == BLS_LINK_STATE_CONN) {
		// In libble_8258.a(ll_pm.o), blt_brx_sleep() passes wakeup_tick == reg_system_tick_irq
		// with interrupts enabled, and cpu_sleep_wakeup_32k_rc() re-enables reg_irq_en = 1
		// after spinning to wakeup_tick BEFORE returning to blt_brx_sleep(). Without this
		// guard, irq_slave_system_timer -> blt_brx_start() fires before blt_brx_sleep() runs
		// rf_ble_1m_param_init(), which then clobbers active RF/modem registers mid-RX.
		// Wake 150 us early and keep IRQs masked until user_set_rf_power() (SUSPEND_EXIT),
		// which runs immediately after rf_ble_1m_param_init().
		wakeup_tick -= 150 * CLOCK_16M_SYS_TIMER_CLK_1US;
		s_suspend_irq_saved = irq_disable();
	}

	return cpu_sleep_wakeup_32k_rc(sleep_mode, wakeup_src, wakeup_tick);
}

static inline void blc_pm_select_internal_32k_crystal(void) {
	cpu_sleep_wakeup = ble_cpu_sleep_wakeup_32k_rc;
	pm_tim_recover = pm_tim_recover_32k_rc;
	blt_miscParam.pm_enter_en = 1;
}

void user_ble_init(bool isRetention){
	s_suspend_irq_saved = 0;
	s_pending_epd_display = 0;
	s_epd_display_in_progress = 0;
	blc_pm_select_internal_32k_crystal();
	blc_rf_pa_cb = power_tracker_ble_rf_cb;
	sendTerminate_before_enterDeep = 0;
#if(BLE_APP_PM_ENABLE)
	bls_pm_conditionCbRegister(app_ble_pm_condition);
#endif
	if(isRetention){
		// In pure-BLE SDK (liblt_8258.a), blt_dma_tx_rptr and sdk_mainLoop_run_flag
		// reside in non-retention .bss and are zeroed by cstartup_8258.S on every
		// deep-retention wakeup because hardware DMA TX wptr (0x800c2b) resets to 0
		// in deep sleep and blt_sdk_main_loop() must allow one full main-loop pass
		// before re-entering sleep. In Dual-Mode SDK (libble_8258.a), .bss is retained,
		// so we must explicitly zero both on deep-retention wakeup.
		extern u8 blt_dma_tx_rptr;
		extern u32 sdk_mainLoop_run_flag;
		blt_dma_tx_rptr = 0;
		sdk_mainLoop_run_flag = 0;

		blc_ll_initBasicMCU();   //mandatory
		blc_ll_recoverDeepRetention();

		// Guard against hardware equality comparator miss if cold-flash wakeup
		// + 32kHz RC jitter reached blt_next_event_tick before reg_system_tick_irq was armed.
		if (blc_ll_getCurrentState() == BLS_LINK_STATE_CONN) {
			u32 now = clock_time();
			if ((u32)(now + 160 - reg_system_tick_irq) < BIT(30)) {
				reg_system_tick_irq = now + 160;
			}
		}
	}else{
		user_ble_normal_init();
	}
}


int blt_pm_proc(void) {
#if 0
	if(sendTerminate_before_enterDeep == 1) {  //sending Terminate and wait for ack before enter deepsleep
	}
	else if(sendTerminate_before_enterDeep == 2) { //Terminate OK
		return 1;
	}
	return 0;
#else
	return sendTerminate_before_enterDeep == 2;
#endif
}

static void epd_ble_fetch_task(void) {
    if (!s_fetch.active) return;

    u8 tx_used = (u8)(blt_txfifo.wptr - blt_txfifo.rptr);
    if (tx_used >= (TX_FIFO_NUM - 2)) {
        return;
    }

    uint16_t total_fetch_size = EPD_PLANE_SIZE;
    if (s_fetch.slot >= EPD_USER_SLOT_START && s_fetch.slot < (EPD_USER_SLOT_START + EPD_USER_SLOT_COUNT)) {
        epd_slot_header_t hdr;
        if (epd_get_slot_header(s_fetch.slot, &hdr)) {
            total_fetch_size = sizeof(epd_slot_header_t) + hdr.bw_comp_len + hdr.red_comp_len;
            if (total_fetch_size > EPD_SLOT_SIZE) total_fetch_size = EPD_SLOT_SIZE;
        } else {
            total_fetch_size = sizeof(epd_slot_header_t); // Empty/unprogrammed slot
        }
    }

    if (s_fetch.offset >= total_fetch_size) {
        // Send EOF packet (offset = total_fetch_size, len = 0)
        uint8_t resp[6];
        resp[0] = 0x86;
        resp[1] = s_fetch.slot;
        resp[2] = s_fetch.plane;
        resp[3] = (uint8_t)(total_fetch_size & 0xFF);
        resp[4] = (uint8_t)((total_fetch_size >> 8) & 0xFF);
        resp[5] = 0; // EOF marker
        if (bls_att_pushNotifyData(EPD_CMD_DP_H, resp, 6) == BLE_SUCCESS) {
            s_fetch.active = 0;
            s_stream_activity_tick = 0;
            bls_pm_setManualLatency(19);
            bls_l2cap_requestConnParamUpdate(40, 40, 19, 800);
        }
        return;
    }

    uint8_t chunk_len = 20;
    if (s_fetch.offset + chunk_len > total_fetch_size) {
        chunk_len = total_fetch_size - s_fetch.offset;
    }

    uint8_t resp[6 + 20];
    resp[0] = 0x86; // Chunk marker
    resp[1] = s_fetch.slot;
    resp[2] = s_fetch.plane;
    resp[3] = (uint8_t)(s_fetch.offset & 0xFF);
    resp[4] = (uint8_t)((s_fetch.offset >> 8) & 0xFF);
    resp[5] = chunk_len;

    epd_read_slot_chunk(s_fetch.slot, s_fetch.plane, s_fetch.offset, &resp[6], chunk_len);

    if (bls_att_pushNotifyData(EPD_CMD_DP_H, resp, 6 + chunk_len) == BLE_SUCCESS) {
        s_fetch.offset += chunk_len;
        s_stream_activity_tick = clock_time() | 1;
    }
}

#if DEBUG
static void epd_ble_log_task(void) {
    if (!s_log_pull_active && !s_log_stream_enabled) return;

    uint16_t latest = log_ring_get_latest_seq();
    if (latest == 0) {
        if (s_log_pull_active) {
            uint8_t resp[5] = {0x8A, 0, 0, 0, 0};
            if (bls_att_pushNotifyData(EPD_CMD_DP_H, resp, sizeof(resp)) == BLE_SUCCESS) {
                s_log_pull_active = 0;
                s_stream_activity_tick = 0;
            }
        }
        return;
    }

    uint16_t oldest = log_ring_get_oldest_seq();
    if (s_log_client_seq == 0) {
        if (oldest > 0) s_log_client_seq = oldest - 1;
    } else if (s_log_client_seq > latest) {
        // Device reboot or log clear detected
        s_log_client_seq = (oldest > 0) ? (oldest - 1) : 0;
    } else if (s_log_client_seq < oldest - 1) {
        // Fast-forward client to oldest remaining entry
        s_log_client_seq = oldest - 1;
    }

    if (s_log_client_seq >= latest) {
        if (s_log_pull_active) {
            uint8_t resp[5] = {0x8A, 0, 0, (uint8_t)(latest & 0xFF), (uint8_t)((latest >> 8) & 0xFF)};
            if (bls_att_pushNotifyData(EPD_CMD_DP_H, resp, sizeof(resp)) == BLE_SUCCESS) {
                s_log_pull_active = 0;
                s_stream_activity_tick = 0;
            }
        }
        return;
    }

    uint16_t target_seq = s_log_client_seq + 1;
    ble_log_entry_t entry;
    if (log_ring_get_entry(target_seq, &entry)) {
        uint8_t text_len = strlen(entry.text);
        if (text_len > LOG_ENTRY_TEXT_LEN) text_len = LOG_ENTRY_TEXT_LEN;

        uint8_t resp[7 + LOG_ENTRY_TEXT_LEN];
        resp[0] = 0x8A;
        resp[1] = (uint8_t)(entry.seq & 0xFF);
        resp[2] = (uint8_t)((entry.seq >> 8) & 0xFF);
        resp[3] = (uint8_t)(entry.timestamp_ms & 0xFF);
        resp[4] = (uint8_t)((entry.timestamp_ms >> 8) & 0xFF);
        resp[5] = (uint8_t)((entry.timestamp_ms >> 16) & 0xFF);
        resp[6] = (uint8_t)((entry.timestamp_ms >> 24) & 0xFF);
        memcpy(&resp[7], entry.text, text_len);

        if (bls_att_pushNotifyData(EPD_CMD_DP_H, resp, 7 + text_len) == BLE_SUCCESS) {
            s_log_client_seq = target_seq;
            s_stream_activity_tick = clock_time() | 1;
        }
    } else {
        s_log_client_seq = target_seq;
    }
}
#else
#define epd_ble_log_task() do {} while(0)
#endif

void ble_prototype_pump(void) {
    if (settings.active_mode == DEVICE_MODE_BLE) {
        blt_sdk_main_loop();
#if DEBUG
        epd_ble_log_task();
#endif
    }
}

void ble_display_poll(void) {
    if (device_in_connection_state && settings.active_mode == DEVICE_MODE_BLE) {
        bls_pm_setSuspendMask(SUSPEND_DISABLE);
        bls_pm_setManualLatency(0);
        blt_sdk_main_loop();
    }
}

void ble_task(void) {
	if (s_pending_epd_display || s_epd_display_in_progress) {
		bls_pm_setManualLatency(0);
	}
	blt_sdk_main_loop();
	epd_ble_fetch_task();
	epd_ble_log_task();

#if DEBUG
	if ((s_stream_upload_active || s_fetch.active || s_log_pull_active) &&
	    s_stream_activity_tick != 0 &&
	    clock_time_exceed(s_stream_activity_tick, 30 * 1000 * 1000)) {
		DEBUG_LOG("BLE", "Stream inactivity timeout (30s); restoring sleep latency");
		s_stream_upload_active = 0;
		s_fetch.active = 0;
		s_log_pull_active = 0;
		s_stream_activity_tick = 0;
		bls_pm_setManualLatency(19);
		bls_l2cap_requestConnParamUpdate(40, 40, 19, 800);
	}
#else
	if ((s_stream_upload_active || s_fetch.active) &&
	    s_stream_activity_tick != 0 &&
	    clock_time_exceed(s_stream_activity_tick, 30 * 1000 * 1000)) {
		s_stream_upload_active = 0;
		s_fetch.active = 0;
		s_stream_activity_tick = 0;
		bls_pm_setManualLatency(19);
		bls_l2cap_requestConnParamUpdate(40, 40, 19, 800);
	}
#endif

	if (s_pending_proto_op) {
		uint8_t op = s_pending_proto_op;
		uint32_t addr = s_pending_proto_addr;
		uint8_t flags = s_pending_proto_flags;
		s_pending_proto_op = 0;

		int ret = 0;
		uint32_t dur_ms = 0;
		s_proto_running = 1;

		if (op == 1) { // Commit & Execute
			uint16_t total_bytes = s_upload_telemetry.confirmed_offset;
			if (!epd_prototype_commit(addr, s_ota_cache, total_bytes)) {
				ret = -1;
			} else if (flags & 0x01) {
				ret = epd_prototype_execute(addr, &dur_ms);
			}
		} else if (op == 2) { // Re-execute
			ret = epd_prototype_execute(addr, &dur_ms);
		}

		s_proto_running = 0;
		s_proto_result_ret = (int16_t)ret;
		s_proto_result_dur = dur_ms;
		s_proto_result_pending = 1;

		bls_pm_setManualLatency(19);
		bls_l2cap_requestConnParamUpdate(40, 40, 19, 800);
	}

	if (s_proto_result_pending) {
		uint8_t resp[7];
		resp[0] = 0x89; // Snippet execution result marker
		resp[1] = (uint8_t)(s_proto_result_ret & 0xFF);
		resp[2] = (uint8_t)((s_proto_result_ret >> 8) & 0xFF);
		resp[3] = (uint8_t)(s_proto_result_dur & 0xFF);
		resp[4] = (uint8_t)((s_proto_result_dur >> 8) & 0xFF);
		resp[5] = (uint8_t)((s_proto_result_dur >> 16) & 0xFF);
		resp[6] = (uint8_t)((s_proto_result_dur >> 24) & 0xFF);
		if (bls_att_pushNotifyData(EPD_CMD_DP_H, resp, sizeof(resp)) == BLE_SUCCESS) {
			s_proto_result_pending = 0;
		}
	}

	if (s_pending_mode_switch && clock_time_exceed(s_pending_mode_switch_tick, 300 * 1000)) {
		uint8_t target = s_pending_mode_switch;
		s_pending_mode_switch = 0;
		DEBUG_LOG("BLE", "Executing deferred mode switch to %u", target);
		mode_switch_to(target);
	}
	if (s_pending_reboot && clock_time_exceed(s_pending_reboot_tick, 300 * 1000)) {
		s_pending_reboot = 0;
		DEBUG_LOG("BLE", "Executing deferred reboot");
		start_reboot();
	}
	if (s_pending_epd_display &&
	    clock_time_exceed(s_pending_epd_display_tick, 50 * 1000)) {
		s_pending_epd_display = 0;
		s_epd_display_in_progress = 1;
		bls_pm_setSuspendMask(SUSPEND_DISABLE);
		bls_pm_setManualLatency(0);
		DEBUG_LOG("BLE", "Executing deferred EPD display (Slot %u, Style %u)",
		          settings.active_slot, settings.render_style);
		epd_display_slot(settings.active_slot, settings.render_style);
		s_epd_display_in_progress = 0;
	}
}

void ble_pm_task(void) {
#if(BLE_APP_PM_ENABLE)
#if USE_BLE_OTA
	if (ota_is_working) {
		bls_pm_setSuspendMask(SUSPEND_DISABLE);
		return;
	}
#endif
#if DEBUG
	if (s_fetch.active || s_stream_upload_active || s_log_pull_active || s_log_stream_enabled ||
#else
	if (s_fetch.active || s_stream_upload_active ||
#endif
	    s_pending_proto_op || s_proto_running || s_proto_result_pending ||
	    s_pending_mode_switch || s_pending_reboot ||
	    s_pending_epd_display || s_epd_display_in_progress) {
		if (s_pending_epd_display || s_epd_display_in_progress) {
			bls_pm_setManualLatency(0);
		}
		bls_pm_setSuspendMask(SUSPEND_DISABLE);
		return;
	}
	if (!nfc_fm11nc08_is_active() && gpio_read(GPIO_NFC_IRQ)) {
		cpu_set_gpio_wakeup(GPIO_NFC_IRQ, Level_Low, 1);
		if (epd_is_busy()) {
			cpu_set_gpio_wakeup(GPIO_EPD_BUSY, Level_High, 1);
			if (device_in_connection_state) {
				// Use SUSPEND_CONN (keeps digital GPIO drivers latched for EPD power
				// and red LED at ~35 uA) with a conservative latency of 3 (200 ms).
				bls_pm_setManualLatency(3);
			}
			bls_pm_setSuspendMask(SUSPEND_ADV | SUSPEND_CONN);
		} else {
			cpu_set_gpio_wakeup(GPIO_EPD_BUSY, Level_High, 0);
			bls_pm_setSuspendMask(SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV |
			                      SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN);
		}
	} else {
		bls_pm_setSuspendMask(SUSPEND_DISABLE);
	}
#else
	bls_pm_setSuspendMask(SUSPEND_DISABLE);
#endif
}



