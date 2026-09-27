#include "tl_common.h"
#include "zcl_include.h"
#include "zb_endpoint_cfg.h"
#include "flash_eep.h"
#include "epd.h"
#include "led.h"
#include "nfc_fm11nc08.h"
#include "mode_switch.h"
#include "zb_app.h"
#include "debug_uart.h"
#include "../power_tracker.h"

static void app_zclWriteReqCmd(uint8_t endPoint, uint16_t clusterId, zclWriteCmd_t *pWriteReqCmd) {
    if (clusterId == ZCL_CLUSTER_CUSTOM_EPAPER) {
        bool epd_changed = false;
        bool led_changed = false;
        for (int i = 0; i < pWriteReqCmd->numAttr; i++) {
            zclWriteRec_t *attr = &pWriteReqCmd->attrList[i];
            if (attr->attrID == ZCL_ATTRID_EPAPER_ACTIVE_SLOT) {
                uint8_t slot = attr->attrData[0];
                DEBUG_LOG("ZCL", "Write activeSlot = %u", slot);
                if (slot < EPD_SLOT_COUNT) {
                    settings.active_slot = slot;
                    g_zcl_epaperAttrs.activeSlot = slot;
                    epd_changed = true;
                }
            } else if (attr->attrID == ZCL_ATTRID_EPAPER_RENDER_STYLE) {
                uint8_t style = attr->attrData[0];
                DEBUG_LOG("ZCL", "Write renderStyle = %u", style);
                if (style < STYLE_COUNT) {
                    settings.render_style = style;
                    g_zcl_epaperAttrs.renderStyle = style;
                    epd_changed = true;
                }
            } else if (attr->attrID == ZCL_ATTRID_EPAPER_ACTIVE_MODE) {
                uint8_t mode = attr->attrData[0];
                DEBUG_LOG("ZCL", "Write activeMode = %u (%s)", mode, (mode == DEVICE_MODE_BLE) ? "BLE" : "Zigbee");
                if (mode == DEVICE_MODE_BLE || mode == DEVICE_MODE_ZIGBEE) {
                    g_zcl_epaperAttrs.activeMode = mode;
                    zb_schedule_mode_switch(mode);
                }
            } else if (attr->attrID == ZCL_ATTRID_EPAPER_LED_STATE) {
                uint8_t mask = attr->attrData[0];
                DEBUG_LOG("ZCL", "Write ledState = 0x%02X", mask);
                led_set(mask);
                g_zcl_epaperAttrs.ledState = led_get_state();
                led_changed = true;
            }
        }

        if (epd_changed) {
            flash_eep_save();
            epd_display_slot(settings.active_slot, settings.render_style);
            g_zcl_epaperAttrs.refreshCount = settings.screen_refresh_count;
            app_update_nfc_telemetry();
            zb_epaper_report_attrs();
        } else if (led_changed) {
            zb_epaper_report_attrs();
        }
    }
}

void app_zclProcessIncomingMsg(zclIncoming_t *pInHdlrMsg) {
    uint16_t cluster = pInHdlrMsg->msg->indInfo.cluster_id;
    uint8_t endPoint = pInHdlrMsg->msg->indInfo.dst_ep;

    if (cluster == ZCL_CLUSTER_GEN_ON_OFF) {
        DEBUG_LOG("ZCL", "On/Off cluster command: 0x%02X", pInHdlrMsg->hdr.cmd);
        if (pInHdlrMsg->hdr.cmd == 0x01) { // On
            led_on(LED_RED);
        } else if (pInHdlrMsg->hdr.cmd == 0x00) { // Off
            led_off(LED_RED);
        } else if (pInHdlrMsg->hdr.cmd == 0x02) { // Toggle
            led_toggle(LED_RED);
        }
        g_zcl_epaperAttrs.ledState = led_get_state();
        zb_epaper_report_attrs();
        return;
    }

    switch (pInHdlrMsg->hdr.cmd) {
        case ZCL_CMD_READ:
            if (cluster == ZCL_CLUSTER_CUSTOM_EPAPER) {
                power_stats_t pstats;
                power_tracker_get_stats(&pstats);
                g_zcl_epaperAttrs.wakeupCount    = pstats.wakeup_count;
                g_zcl_epaperAttrs.wakeupDuration = pstats.wakeup_duration_ms;
                g_zcl_epaperAttrs.txDuration     = pstats.tx_duration_ms;
                g_zcl_epaperAttrs.rxDuration     = pstats.rx_duration_ms;
            }
            break;
        case ZCL_CMD_WRITE:
        case ZCL_CMD_WRITE_UNDIVIDED:
        case ZCL_CMD_WRITE_NO_RSP:
            app_zclWriteReqCmd(endPoint, cluster, pInHdlrMsg->attrCmd);
            break;
        default:
            break;
    }
}

status_t app_identifyCb(zclIncomingAddrInfo_t *pAddrInfo, uint8_t cmdId, void *cmdPayload) {
    if (cmdId == ZCL_CMD_IDENTIFY) {
        zcl_identifyCmd_t *p = (zcl_identifyCmd_t *)cmdPayload;
        DEBUG_LOG("ZCL", "Identify command: identifyTime=%u", p ? p->identifyTime : 0);
        led_blink(LED_GREEN, 200);
    } else if (cmdId == ZCL_CMD_TRIGGER_EFFECT) {
        zcl_triggerEffect_t *p = (zcl_triggerEffect_t *)cmdPayload;
        DEBUG_LOG("ZCL", "Identify Trigger Effect: effectId=0x%02X, var=0x%02X",
                  p ? p->effectId : 0, p ? p->effectVariant : 0);
        led_blink(LED_GREEN, 200);
    }
    return ZCL_STA_SUCCESS;
}

status_t app_basicCb(zclIncomingAddrInfo_t *pAddrInfo, uint8_t cmdId, void *cmdPayload) {
    if (cmdId == ZCL_CMD_BASIC_RESET_FAC_DEFAULT) {
        DEBUG_LOG("ZCL", "Basic Reset to Factory Defaults received");
        mode_switch_zigbee_reset();
    }
    return ZCL_STA_SUCCESS;
}
