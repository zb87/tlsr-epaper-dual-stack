#include "tl_common.h"
#include "app_config.h"
#include "flash_eep.h"
#include "battery.h"
#include "epd.h"
#include "nfc_fm11nc08.h"
#include "led.h"
#include "u_printf.h"
#include "power_tracker.h"

#if NFC_ENABLE_DIAG
_attribute_custom_bss_ char nfc_diag_ble_buf[256];
#endif

// Hardware I2C / Chip Presence
RAM static bool fm11_present = false;
RAM static uint8_t fm11_i2c_addr = FM11NC08_I2C_ADDR_W; // 0xAE default, or 0xA0

// Cache last transmitted RF response for ISO 14443-4 R(NAK) retransmission
RAM static uint8_t last_tx_buf[32] = {0};
RAM static uint8_t last_tx_len = 0;

// Type 4 Tag File IDs
#define T4T_FILE_NONE   0
#define T4T_FILE_CC     1
#define T4T_FILE_NDEF   2

// T4T Capability Container (CC) File (15 bytes)
// MLe = 0x001A (26 bytes) and MLc = 0x0014 (20 bytes) guarantee that all APDU frames
// fit inside FM11NC08's 32-byte hardware FIFO without overflow.
// Phone readers automatically loop READ BINARY requests to read NDEF files larger than MLe.
static const uint8_t t4t_cc_file[15] = {
    0x00, 0x0F, // CCLEN = 15 bytes
    0x20,       // Mapping Version 2.0
    0x00, 0x1A, // MLe = 26 bytes max R-APDU size (fits in 32B FIFO)
    0x00, 0x14, // MLc = 20 bytes max C-APDU size (fits in 32B FIFO on write)
    0x04, 0x06, // NDEF File Control TLV (T=04, L=06)
    0xE1, 0x04, // NDEF File ID = 0xE104
    0x01, 0x80, // Max NDEF File Size = 384 bytes (0x0180)
    0x00,       // Read access: granted without security
    0x00        // Write access: granted without security
};

// T4T NDEF File RAM Buffer: [NLEN_MSB, NLEN_LSB, NDEF_RECORD...]
// Placed in .custom_bss (upper SRAM) to preserve lower 32KB retention SRAM.
// Populated dynamically by app_update_nfc_telemetry() on boot and during NFC sessions.
_attribute_custom_bss_ static uint8_t t4t_ndef_file[384];

// Real-time NFC Session State Tracking
RAM static bool     session_active = false;
RAM static uint32_t session_start_tick = 0;
RAM static uint32_t last_activity_tick = 0;
RAM static bool     write_committed = false;
RAM static bool     write_occurred = false;
RAM static uint8_t  selected_file = T4T_FILE_NONE;
RAM static bool     apdu_processed = false;

#if NFC_ENABLE_DIAG
// NFC Diagnostics tracking data
RAM static nfc_diag_data_t nfc_diag = {0};
#define NFC_DIAG_INC(var)             ((var)++)
#define NFC_DIAG_SET(var, val)        ((var) = (val))
#define NFC_DIAG_MEMCPY(dst, src, sz) memcpy((dst), (src), (sz))
#define NFC_UPDATE_DIAG_STR()         nfc_fm11nc08_get_diag_string(nfc_diag_ble_buf, sizeof(nfc_diag_ble_buf))
#else
#define NFC_DIAG_INC(var)             ((void)0)
#define NFC_DIAG_SET(var, val)        ((void)0)
#define NFC_DIAG_MEMCPY(dst, src, sz) ((void)0)
#define NFC_UPDATE_DIAG_STR()         ((void)0)
void nfc_fm11nc08_get_debug_log(uint8_t *dst) { (void)dst; }
#endif
RAM static bool last_irq_pin_state = true;

#include "debug_uart.h"
#if DEBUG
#define nfc_uart_printf(fmt, ...) debug_printf(fmt, ##__VA_ARGS__)
#else
#define nfc_uart_printf(fmt, ...) do {} while (0)
#endif

#if NFC_ENABLE_DIAG
// APDU debug log buffer (last 4 processed APDUs: [rx[0], cla, ins, p1, p2, p3, sw1, sw2])
RAM static uint8_t  nfc_debug_log[4][8] = {{0}};
RAM static uint8_t  nfc_debug_idx = 0;

void nfc_fm11nc08_get_debug_log(uint8_t *dst) {
    if (!dst) return;
    memcpy(dst, nfc_debug_log, sizeof(nfc_debug_log));
}

void nfc_fm11nc08_get_diag(nfc_diag_data_t *dst) {
    if (!dst) return;
    nfc_diag.irq_pin_level = gpio_read(GPIO_NFC_IRQ) ? 1 : 0;
    memcpy(dst, &nfc_diag, sizeof(nfc_diag_data_t));
}

int nfc_fm11nc08_get_diag_string(char *dst, int dst_max_len) {
    if (!dst || dst_max_len < 32) return 0;
    nfc_diag.irq_pin_level = gpio_read(GPIO_NFC_IRQ) ? 1 : 0;

    int pos = snprintf(dst, dst_max_len,
        "FM11:%s(0x%02X,v%02X) PC4:%u(ev:%u) CFG:%02X%02X%02X%02X ST:%02X RF:%02X IRQ:%02X W:%u F:%02X A:%02X S:%u AP:%u WR:%u RX[%u]:%02X%02X%02X%02X TX:%02X%02X",
        nfc_diag.present ? "OK" : "FAIL",
        nfc_diag.i2c_addr, nfc_diag.vendor_id,
        nfc_diag.irq_pin_level, nfc_diag.irq_fall_count,
        nfc_diag.user_cfg[0], nfc_diag.user_cfg[1], nfc_diag.user_cfg[2], nfc_diag.user_cfg[3],
        nfc_diag.status_reg, nfc_diag.rf_status_reg,
        nfc_diag.last_main_irq, nfc_diag.last_fifo_wordcnt,
        nfc_diag.last_fifo_irq, nfc_diag.last_aux_irq,
        nfc_diag.session_count, nfc_diag.apdu_count, nfc_diag.write_count,
        nfc_diag.last_rx_len,
        nfc_diag.last_rx_bytes[0], nfc_diag.last_rx_bytes[1],
        nfc_diag.last_rx_bytes[2], nfc_diag.last_rx_bytes[3],
        nfc_diag.last_tx_bytes[0], nfc_diag.last_tx_bytes[1]
    );

    if (pos < 0) return 0;
    if (pos >= dst_max_len) {
        dst[dst_max_len - 1] = '\0';
        return dst_max_len - 1;
    }

    // Append up to 4 recent APDUs from history: [0]INS>SW [1]INS>SW ...
    uint8_t count = (nfc_debug_idx > 4) ? 4 : nfc_debug_idx;
    uint8_t start = (nfc_debug_idx > 4) ? (nfc_debug_idx - 4) : 0;
    for (uint8_t i = 0; i < count && pos < dst_max_len - 16; i++) {
        uint8_t idx = (start + i) & 3;
        int n = snprintf(&dst[pos], dst_max_len - pos, " [%u]%02X%02X>%02X%02X",
                         i,
                         nfc_debug_log[idx][1], nfc_debug_log[idx][2],
                         nfc_debug_log[idx][6], nfc_debug_log[idx][7]);
        if (n < 0 || n >= dst_max_len - pos) break;
        pos += n;
    }

    return pos;
}
#endif

// Low-Level NFC Chip Select (PC6) Control
static inline void nfc_cs_low(void) {
    gpio_write(GPIO_NFC_CS, 0);
    WaitUs(50);
}

static inline void nfc_cs_high(void) {
    gpio_write(GPIO_NFC_CS, 1);
    WaitUs(10);
}

static void nfc_hw_i2c_init(void) {
    i2c_gpio_set(I2C_GPIO_GROUP_C0C1);
    i2c_master_init((uint8_t)(CLOCK_SYS_CLOCK_HZ / (4 * 400000)));
    i2c_set_id(fm11_i2c_addr);
}

// Probe an 8-bit I2C write address (sends START + ID + STOP)
static bool nfc_i2c_probe_addr(uint8_t addr) {
    unsigned char r = irq_disable();
    i2c_set_id(addr);
    reg_i2c_ctrl = FLD_I2C_CMD_START | FLD_I2C_CMD_ID | FLD_I2C_CMD_STOP;
    uint32_t t = 10000;
    while ((reg_i2c_status & FLD_I2C_CMD_BUSY) && --t);
    bool ack = (t > 0) && !(reg_i2c_status & FLD_I2C_NAK);
    irq_restore(r);
    return ack;
}

// Low-level hardware I2C write: 16-bit address + data buffer
static bool nfc_hw_i2c_write(uint16_t reg_addr, const uint8_t *data, uint16_t len) {
    unsigned char r = irq_disable();
    i2c_set_id(fm11_i2c_addr);
    i2c_write_series(reg_addr, 2, (uint8_t *)data, len);
    bool ok = !(reg_i2c_status & FLD_I2C_NAK);
    irq_restore(r);
    return ok;
}

// Low-level hardware I2C read: 16-bit address + Repeated Start + receive data
static bool nfc_hw_i2c_read(uint16_t reg_addr, uint8_t *data, uint16_t len) {
    if (len == 0 || !data) return true;
    unsigned char r = irq_disable();
    i2c_set_id(fm11_i2c_addr);
    i2c_read_series(reg_addr, 2, data, len);
    irq_restore(r);
    return true;
}

static bool nfc_read_reg(uint16_t reg_addr, uint8_t *val) {
    nfc_cs_low();
    bool ok = nfc_hw_i2c_read(reg_addr, val, 1);
    nfc_cs_high();
    return ok;
}

static bool nfc_write_reg(uint16_t reg_addr, uint8_t val) {
    nfc_cs_low();
    uint8_t d = val;
    bool ok = nfc_hw_i2c_write(reg_addr, &d, 1);
    nfc_cs_high();
    return ok;
}

static bool nfc_read_eeprom(uint16_t addr, uint8_t *buf, uint8_t len) {
    if (len == 0) return true;
    nfc_cs_low();
    bool ok = nfc_hw_i2c_read(addr, buf, len);
    nfc_cs_high();
    return ok;
}

static bool nfc_write_eeprom(uint16_t addr, const uint8_t *buf, uint8_t len) {
    while (len > 0) {
        uint8_t page_offset = (uint8_t)(addr % 16);
        uint8_t chunk = 16 - page_offset;
        if (chunk > len) chunk = len;

        nfc_cs_low();
        bool ok = nfc_hw_i2c_write(addr, buf, chunk);
        nfc_cs_high();
        if (!ok) return false;

        WaitMs(10); // tWR: EEPROM programming cycle

        addr += chunk;
        buf += chunk;
        len -= chunk;
    }
    return true;
}

static bool nfc_read_fifo(uint8_t *buf, uint8_t len) {
    if (len == 0) return true;
    nfc_cs_low();
    bool ok = nfc_hw_i2c_read(FM11_REG_FIFO_ACCESS, buf, len);
    nfc_cs_high();
    return ok;
}

static bool nfc_write_fifo(const uint8_t *buf, uint8_t len) {
    if (len == 0) return true;
    nfc_cs_low();
    bool ok = nfc_hw_i2c_write(FM11_REG_FIFO_ACCESS, buf, len);
    nfc_cs_high();
    return ok;
}

static void nfc_flush_fifo(void) {
    nfc_cs_low();
    uint8_t val = 0xFF;
    nfc_hw_i2c_write(FM11_REG_FIFO_CLEAR, &val, 1);
    nfc_cs_high();
}

static void nfc_read_irqs(uint8_t *main_irq, uint8_t *fifo_irq, uint8_t *aux_irq) {
    uint8_t irqs[3] = {0};
    nfc_cs_low();
    nfc_hw_i2c_read(FM11_REG_MAIN_IRQ, irqs, 3);
    nfc_cs_high();
    if (main_irq) *main_irq = irqs[0];
    if (fifo_irq) *fifo_irq = irqs[1];
    if (aux_irq)  *aux_irq  = irqs[2];
}

void nfc_fm11nc08_clear_irq(void) {
    if (!fm11_present) return;
    uint8_t m = 0, f = 0, a = 0;
    nfc_read_irqs(&m, &f, &a);
    nfc_flush_fifo();
}

bool nfc_fm11nc08_is_present(void) {
    return fm11_present;
}

bool nfc_fm11nc08_is_active(void) {
    return session_active || !gpio_read(GPIO_NFC_IRQ);
}

bool nfc_fm11nc08_init(bool is_cold_boot) {
    // 1. Initialize Hardware I2C controller on PC0/PC1
    nfc_hw_i2c_init();

    // 2. Configure PC6 (NFC_CS) as Output, Default HIGH (Idle/Standby for Contactless RF)
    gpio_set_func(GPIO_NFC_CS, AS_GPIO);
    gpio_set_output_en(GPIO_NFC_CS, 1);
    gpio_set_input_en(GPIO_NFC_CS, 0);
    gpio_setup_up_down_resistor(GPIO_NFC_CS, PM_PIN_PULLUP_10K);
    gpio_write(GPIO_NFC_CS, 1);

    // 3. Configure PC4 (NFC_IRQ): Input with 10K pull-up (Active LOW on RF field detect)
    gpio_set_func(GPIO_NFC_IRQ, AS_GPIO);
    gpio_set_output_en(GPIO_NFC_IRQ, 0);
    gpio_set_input_en(GPIO_NFC_IRQ, 1);
    gpio_setup_up_down_resistor(GPIO_NFC_IRQ, PM_PIN_PULLUP_10K);

    // Register PC4 as Level_Low wake-up source from deep retention sleep
    cpu_set_gpio_wakeup(GPIO_NFC_IRQ, Level_Low, 1);

    if (is_cold_boot || !fm11_present) {
        WaitMs(5); // Power stabilization delay

        nfc_cs_low();
        // Probe I2C address: Test 0xAE (factory), 0xA0 (ATC), then scan
        if (nfc_i2c_probe_addr(0xAE)) {
            fm11_i2c_addr = 0xAE;
            fm11_present = true;
        } else if (nfc_i2c_probe_addr(0xA0)) {
            fm11_i2c_addr = 0xA0;
            fm11_present = true;
        } else {
            for (uint8_t a = 0x08; a <= 0x77; a++) {
                uint8_t addr = a << 1;
                if (nfc_i2c_probe_addr(addr)) {
                    fm11_i2c_addr = addr;
                    fm11_present = true;
                    break;
                }
            }
        }
        nfc_cs_high();

#if NFC_ENABLE_DIAG
        nfc_diag.present = fm11_present;
        nfc_diag.i2c_addr = fm11_i2c_addr;
        nfc_diag.irq_pin_level = gpio_read(GPIO_NFC_IRQ);
#endif

        if (fm11_present) {
#if NFC_ENABLE_DIAG
            // Read hardware vendor ID byte from EEPROM 0x0000
            nfc_read_eeprom(0x0000, &nfc_diag.vendor_id, 1);
#endif

            // Visual feedback: 2 short green blinks
            led_blink(LED_GREEN, 80);
            WaitMs(60);
            led_blink(LED_GREEN, 80);

            bool need_reset = false;

            // 1. Configure Level-4 NC (Channel) Mode in USER_CFG:
            // Restore Hanshow factory tuned configuration:
            // USER_CFG0 = 0x91 (Channel mode, VOUT/modulator drive enabled, open-drain active-low IRQ)
            // USER_CFG1 = 0x82 (rf_inventory_en = 1, ISO 14443-4 responses enabled)
            // USER_CFG2 = 0x98 (factory load modulation depth and drive strength)
            // USER_CFG_CHK = ~(0x91 ^ 0x82 ^ 0x98) = 0x74
            static const uint8_t target_cfg[4] = {0x91, 0x82, 0x98, 0x74};
            uint8_t cur_cfg[4] = {0};
            nfc_read_eeprom(FM11_EEPROM_USER_CFG, cur_cfg, 4);
            if (memcmp(cur_cfg, target_cfg, 4) != 0) {
                nfc_write_eeprom(FM11_EEPROM_USER_CFG, target_cfg, 4);
                nfc_write_eeprom(FM11_EEPROM_USER_DEF, target_cfg, 4);
                need_reset = true;
            }

            // 2. Configure Standard ISO 14443-4 ATS (Answer to Select) in EEPROM:
            // Block 0xEC (0x03B0):
            //   0x03B0 = ATS TL: 5 bytes total
            //   0x03B1 = ATS T0: 0x72 (b8=0, TC=1, TB=1, TA=1, FSCI=2 -> 32 bytes max frame size)
            static const uint8_t ats_part1[2] = {0x05, 0x72};
            uint8_t cur_ats1[2] = {0};
            nfc_read_eeprom(0x03B0, cur_ats1, 2);
            if (memcmp(cur_ats1, ats_part1, 2) != 0) {
                nfc_write_eeprom(0x03B0, ats_part1, 2);
                memcpy(cur_ats1, ats_part1, 2);
                need_reset = true;
            }
#if NFC_ENABLE_DIAG
            memcpy(nfc_diag.ats_tl_t0, cur_ats1, 2);
#endif

            // Block 0xED (0x03B4):
            //   0x03B4 = ATS TA: 0x80 (106 kbps in both directions)
            //   0x03B5 = ATS TB: 0xA0 (FWI=10 -> 309 ms response timeout, SFGI=0 -> 0 guard time)
            //   0x03B6 = ATS TC: 0x02 (CID supported, NAD not supported)
            static const uint8_t ats_part2[3] = {0x80, 0xA0, 0x02};
            uint8_t cur_ats2[3] = {0};
            nfc_read_eeprom(0x03B4, cur_ats2, 3);
            if (memcmp(cur_ats2, ats_part2, 3) != 0) {
                nfc_write_eeprom(0x03B4, ats_part2, 3);
                memcpy(cur_ats2, ats_part2, 3);
                need_reset = true;
            }
#if NFC_ENABLE_DIAG
            memcpy(nfc_diag.ats_ta_tc, cur_ats2, 3);
#endif

            if (need_reset) {
                // Software reset to reload new EEPROM configuration into hardware registers
                nfc_write_reg(FM11_REG_RESET_SILENCE, 0x55);
                WaitMs(15);
            }

            // Ensure contactless RF interface is active (non-silent)
            nfc_write_reg(FM11_REG_RESET_SILENCE, 0xCC);

            // Explicitly set runtime registers AFTER soft-reset so hardware registers match target_cfg
            nfc_write_reg(FM11_REG_USER_CFG0, target_cfg[0]);
            nfc_write_reg(FM11_REG_USER_CFG1, target_cfg[1]);
            nfc_write_reg(FM11_REG_USER_CFG2, target_cfg[2]);

            // 3. Configure Interrupt Mask: only RxDone unmasked (0xEF)
            nfc_write_reg(FM11_REG_MAIN_IRQ_MASK, 0xEF);
            nfc_write_reg(FM11_REG_FIFO_IRQ_MASK, 0xFF);
            nfc_write_reg(FM11_REG_AUX_IRQ_MASK, 0xFF);

#if NFC_ENABLE_DIAG
            // Readback registers for verification & diagnostics
            nfc_read_eeprom(FM11_EEPROM_USER_CFG, nfc_diag.user_cfg, 4);
            nfc_read_reg(FM11_REG_STATUS, &nfc_diag.status_reg);
            nfc_read_reg(FM11_REG_USER_CFG0, &nfc_diag.reg_user_cfg0);
            nfc_read_reg(FM11_REG_RF_STATUS, &nfc_diag.rf_status_reg);
            nfc_read_reg(FM11_REG_MAIN_IRQ_MASK, &nfc_diag.reg_main_irq_mask);
#endif

            // Clear any latched power-on interrupts & flush FIFO
            nfc_fm11nc08_clear_irq();

            app_update_nfc_telemetry();

            NFC_UPDATE_DIAG_STR();
#if NFC_ENABLE_DIAG
            nfc_uart_printf("[NFC] Init: present=1 addr=0x%02X vid=0x%02X cfg=%02X%02X%02X%02X st=0x%02X\n",
                            fm11_i2c_addr, nfc_diag.vendor_id,
                            nfc_diag.user_cfg[0], nfc_diag.user_cfg[1], nfc_diag.user_cfg[2], nfc_diag.user_cfg[3],
                            nfc_diag.status_reg);
#else
            nfc_uart_printf("[NFC] Init: present=1 addr=0x%02X\n", fm11_i2c_addr);
#endif
        } else {
            NFC_UPDATE_DIAG_STR();
            nfc_uart_printf("[NFC] Init FAIL: chip not detected on I2C\n");
            // Visual feedback: 3 red blinks indicate I2C communication failed
            led_blink(LED_RED, 150);
            WaitMs(100);
            led_blink(LED_RED, 150);
            WaitMs(100);
            led_blink(LED_RED, 150);
        }
    } else {
        // Deep retention sleep wake:
        // FM11NC08 is continuously powered from VDD and retains its register configuration.
        // No redundant I2C transactions needed.
    }

    return fm11_present;
}

#include "flash_eep.h"
#include "battery.h"
#include "epd.h"
#include "zb_api.h"
#include "zigbee/zb_app.h"
#include "zigbee/zb_endpoint_cfg.h"

extern u8 mcuBootAddrGet(void);

void app_update_nfc_telemetry(void) {
    uint16_t vbat = get_battery_mv();
    nfc_fm11nc08_update_telemetry(settings.active_mode, settings.active_slot, settings.render_style,
                                  vbat, 0, settings.screen_refresh_count);
}

bool nfc_fm11nc08_update_telemetry(uint8_t mode, uint8_t slot_idx, uint8_t style, uint16_t vbat_mv, uint8_t temp_c, uint16_t refresh_count) {
    (void)temp_c;
    (void)refresh_count;
    char text[280];
    uint8_t boot_idx = (mcuBootAddrGet() == 0) ? 0 : 1;

    const char *m_str = "BLE";
    if (mode == DEVICE_MODE_ZIGBEE) {
        if (zb_isDeviceJoinedNwk()) {
            m_str = "Zigbee";
        } else if (!zb_isDeviceFactoryNew()) {
            m_str = "Zigbee (lost)";
        } else {
            m_str = "Zigbee (pairing)";
        }
    }

    const char *st_str = (style == STYLE_STANDARD) ? "Standard" :
                         (style == STYLE_BW_STANDARD) ? "Black & White" :
                         (style == STYLE_BW_INVERTED) ? "Black & White Inverted" :
                         (style == STYLE_RW_STANDARD) ? "Red & White" :
                         (style == STYLE_RW_INVERTED) ? "Red & White Inverted" : "Standard";

    power_stats_t pstats = {0};
    power_tracker_get_stats(&pstats);

    u8 ota_err = analog_read(0x3a);
    int text_len;
    if (ota_err != 0) {
        u16 ota_blk = (u16)analog_read(0x3b) | ((u16)analog_read(0x38) << 8);
        text_len = snprintf(text, sizeof(text),
                            "Boot: %u (Err %u#%u)\n"
                            "Mode: %s\n"
                            "Slot: %u\n"
                            "Style: %s\n"
                            "Battery: %u.%02uV\n"
                            "Misc: wake up %u times, %ums, TX %ums, RX %ums",
                            (unsigned int)boot_idx, (unsigned int)ota_err, (unsigned int)ota_blk,
                            m_str, (unsigned int)slot_idx, st_str,
                            (unsigned int)(vbat_mv / 1000), (unsigned int)((vbat_mv % 1000) / 10),
                            (unsigned int)pstats.wakeup_count,
                            (unsigned int)pstats.wakeup_duration_ms,
                            (unsigned int)pstats.tx_duration_ms,
                            (unsigned int)pstats.rx_duration_ms);
    } else {
        text_len = snprintf(text, sizeof(text),
                            "Boot: %u\n"
                            "Mode: %s\n"
                            "Slot: %u\n"
                            "Style: %s\n"
                            "Battery: %u.%02uV\n"
                            "Misc: wake up %u times, %ums, TX %ums, RX %ums",
                            (unsigned int)boot_idx,
                            m_str, (unsigned int)slot_idx, st_str,
                            (unsigned int)(vbat_mv / 1000), (unsigned int)((vbat_mv % 1000) / 10),
                            (unsigned int)pstats.wakeup_count,
                            (unsigned int)pstats.wakeup_duration_ms,
                            (unsigned int)pstats.tx_duration_ms,
                            (unsigned int)pstats.rx_duration_ms);
    }
    if (text_len <= 0 || text_len > 256) return false;

    // Build NFC Forum Type 4 Tag NDEF file:
    // [NLEN_MSB, NLEN_LSB, NDEF_RECORD...]
    uint16_t payload_len = 3 + (uint16_t)text_len;
    if (payload_len <= 255) {
        // Short Record (SR=1): 1-byte payload length
        uint16_t nlen = 4 + payload_len;
        t4t_ndef_file[0] = (uint8_t)(nlen >> 8);
        t4t_ndef_file[1] = (uint8_t)(nlen & 0xFF);
        t4t_ndef_file[2] = 0xD1;        // Header (MB=1, ME=1, SR=1, TNF=0x01 Well-Known)
        t4t_ndef_file[3] = 0x01;        // Type length = 1
        t4t_ndef_file[4] = (uint8_t)payload_len; // Payload length
        t4t_ndef_file[5] = 0x54;        // Type = 'T' (Text)
        t4t_ndef_file[6] = 0x02;        // Status: UTF-8, lang len = 2
        t4t_ndef_file[7] = 'e';
        t4t_ndef_file[8] = 'n';
        memcpy(&t4t_ndef_file[9], text, text_len);
    } else {
        // Normal Record (SR=0): 4-byte payload length
        uint16_t nlen = 7 + payload_len;
        t4t_ndef_file[0] = (uint8_t)(nlen >> 8);
        t4t_ndef_file[1] = (uint8_t)(nlen & 0xFF);
        t4t_ndef_file[2] = 0xC1;        // Header (MB=1, ME=1, SR=0, TNF=0x01 Well-Known)
        t4t_ndef_file[3] = 0x01;        // Type length = 1
        t4t_ndef_file[4] = 0x00;        // 32-bit payload length
        t4t_ndef_file[5] = 0x00;
        t4t_ndef_file[6] = (uint8_t)(payload_len >> 8);
        t4t_ndef_file[7] = (uint8_t)(payload_len & 0xFF);
        t4t_ndef_file[8] = 0x54;        // Type = 'T' (Text)
        t4t_ndef_file[9] = 0x02;        // Status: UTF-8, lang len = 2
        t4t_ndef_file[10] = 'e';
        t4t_ndef_file[11] = 'n';
        memcpy(&t4t_ndef_file[12], text, text_len);
    }

    return true;
}

static bool nfc_parse_written_ndef(nfc_cmd_t *cmd) {
    memset(cmd, 0, sizeof(nfc_cmd_t));
    cmd->type = NFC_CMD_NONE;

    uint16_t nlen = ((uint16_t)t4t_ndef_file[0] << 8) | t4t_ndef_file[1];
    if (nlen == 0 && t4t_ndef_file[2] != 0) {
        uint8_t hdr = t4t_ndef_file[2];
        bool sr = (hdr & 0x10) != 0;
        bool il = (hdr & 0x08) != 0;
        uint8_t type_len = t4t_ndef_file[3];
        uint32_t payload_len = 0;
        if (sr) {
            payload_len = t4t_ndef_file[4];
            nlen = 3 + (il ? 1 : 0) + type_len + (uint16_t)payload_len;
        } else {
            payload_len = ((uint32_t)t4t_ndef_file[4] << 24) | ((uint32_t)t4t_ndef_file[5] << 16) |
                          ((uint32_t)t4t_ndef_file[6] << 8) | t4t_ndef_file[7];
            nlen = 6 + (il ? 1 : 0) + type_len + (uint16_t)payload_len;
        }
        t4t_ndef_file[0] = (uint8_t)(nlen >> 8);
        t4t_ndef_file[1] = (uint8_t)(nlen & 0xFF);
    }
    if (nlen < 5 || nlen > sizeof(t4t_ndef_file) - 2) return false;

    const uint8_t *rec = &t4t_ndef_file[2];
    uint8_t hdr = rec[0];
    bool sr = (hdr & 0x10) != 0;
    bool il = (hdr & 0x08) != 0;
    uint8_t type_len = rec[1];
    uint32_t payload_len = 0;
    uint8_t id_len = 0;
    const uint8_t *type_field = NULL;
    const uint8_t *payload = NULL;

    if (sr) {
        payload_len = rec[2];
        if (il) {
            id_len = rec[3];
            type_field = &rec[4];
        } else {
            type_field = &rec[3];
        }
        payload = type_field + type_len + id_len;
    } else {
        if (nlen < 6 + type_len) return false;
        payload_len = ((uint32_t)rec[2] << 24) | ((uint32_t)rec[3] << 16) |
                      ((uint32_t)rec[4] << 8) | rec[5];
        if (il) {
            id_len = rec[6];
            type_field = &rec[7];
        } else {
            type_field = &rec[6];
        }
        payload = type_field + type_len + id_len;
    }

    if (payload + payload_len > &t4t_ndef_file[2 + nlen]) {
        return false;
    }

    if (type_len == 1 && type_field[0] == 'T') {
        if (payload_len < 1) {
            cmd->type = NFC_CMD_SET_DISPLAY;
            cmd->slot = (settings.active_slot + 1) % EPD_SLOT_COUNT;
            cmd->style = 0xFF;
            return true;
        }
        uint8_t status = payload[0];
        uint8_t lang_len = status & 0x3F;
        if (payload_len <= 1 + lang_len) {
            cmd->type = NFC_CMD_SET_DISPLAY;
            cmd->slot = (settings.active_slot + 1) % EPD_SLOT_COUNT;
            cmd->style = 0xFF;
            return true;
        }
        const char *p = (const char *)&payload[1 + lang_len];
        int text_len = (int)payload_len - 1 - (int)lang_len;

        // Trim whitespace
        while (text_len > 0 && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
            p++;
            text_len--;
        }
        while (text_len > 0 && (p[text_len - 1] == ' ' || p[text_len - 1] == '\t' ||
                                p[text_len - 1] == '\r' || p[text_len - 1] == '\n')) {
            text_len--;
        }

        // Ignore device telemetry readbacks: "[...]"
        if (text_len >= 4 && p[0] == '[' && (p[1] == 'Z' || p[1] == 'B' || p[1] == 'z' || p[1] == 'b')) {
            return false;
        }

        // Convert to lowercase buffer for easy matching
        char str[64];
        int copy_len = (text_len < 63) ? text_len : 63;
        for (int i = 0; i < copy_len; i++) {
            char c = p[i];
            if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
            str[i] = c;
        }
        str[copy_len] = '\0';

        // Ignore device telemetry readbacks in lowercase: "boot: ...", "boot ...", "b0 ...", "b1 ...", "zb ...", "ble ...", "zb-x ...", "zb-p ..."
        if ((copy_len >= 5 && memcmp(str, "boot:", 5) == 0) ||
            (copy_len >= 5 && memcmp(str, "boot ", 5) == 0) ||
            (copy_len >= 3 && memcmp(str, "b0 ", 3) == 0) ||
            (copy_len >= 3 && memcmp(str, "b1 ", 3) == 0) ||
            (copy_len >= 3 && memcmp(str, "zb ", 3) == 0) ||
            (copy_len >= 4 && memcmp(str, "ble ", 4) == 0) ||
            (copy_len >= 5 && memcmp(str, "zb-x ", 5) == 0) ||
            (copy_len >= 5 && memcmp(str, "zb-p ", 5) == 0)) {
            return false;
        }

        // 1. Zigbee Network Reset & Pairing Command: "zigbee:reset", "zb:reset", "reset:zigbee", "zigbee reset"
        if (strstr(str, "reset") != NULL && (strstr(str, "zigbee") != NULL || strstr(str, "zb") != NULL)) {
            cmd->type = NFC_CMD_ZIGBEE_RESET;
            cmd->slot = 0xFF;
            cmd->style = 0xFF;
            return true;
        }

        // 2. Mode Switch Commands: "zigbee", "ble" (switch/toggle command removed per user request)
        if (strstr(str, "zigbee") != NULL || strstr(str, "zb") != NULL) {
            cmd->type = NFC_CMD_SWITCH_ZIGBEE;
            cmd->slot = 0xFF;
            cmd->style = 0xFF;
            return true;
        }
        if (strstr(str, "ble") != NULL) {
            cmd->type = NFC_CMD_SWITCH_BLE;
            cmd->slot = 0xFF;
            cmd->style = 0xFF;
            return true;
        }
        // Explicitly ignore any deprecated "switch" / "toggle" writes
        if (strstr(str, "switch") != NULL || strstr(str, "toggle") != NULL) {
            return false;
        }

        // 3. Slot and Style Configuration
        // Supports setting both slot and style in a single NFC write:
        // - "slot:1 style:2" or "slot:1,style:rw" or "style:3,slot:0"
        // - "1,2" or "1:2" or "set:1,2" or "display:1,2"
        // - "s0", "s1", "s2", "s1 rw", "s1 bwi"
        // - Single setting: "slot:1", "style:rw", "style:1"
        int parsed_slot = -1;
        int parsed_style = -1;

        // Check for style keyword in string
        const char *style_pos = strstr(str, "style");
        if (style_pos == NULL) {
            if (strstr(str, "bwi") != NULL || strstr(str, "rwi") != NULL ||
                strstr(str, "rw") != NULL || strstr(str, "bw") != NULL ||
                strstr(str, "std") != NULL) {
                style_pos = str;
            }
        }
        if (style_pos != NULL) {
            if (strstr(style_pos, "bwi") != NULL || strstr(style_pos, ":2") != NULL ||
                strstr(style_pos, "=2") != NULL || strstr(style_pos, " 2") != NULL) {
                parsed_style = STYLE_BW_INVERTED;
            } else if (strstr(style_pos, "rwi") != NULL || strstr(style_pos, ":4") != NULL ||
                       strstr(style_pos, "=4") != NULL || strstr(style_pos, " 4") != NULL) {
                parsed_style = STYLE_RW_INVERTED;
            } else if (strstr(style_pos, "rw") != NULL || strstr(style_pos, ":3") != NULL ||
                       strstr(style_pos, "=3") != NULL || strstr(style_pos, " 3") != NULL) {
                parsed_style = STYLE_RW_STANDARD;
            } else if (strstr(style_pos, "bw") != NULL || strstr(style_pos, ":1") != NULL ||
                       strstr(style_pos, "=1") != NULL || strstr(style_pos, " 1") != NULL) {
                parsed_style = STYLE_BW_STANDARD;
            } else if (strstr(style_pos, "std") != NULL || strstr(style_pos, ":0") != NULL ||
                       strstr(style_pos, "=0") != NULL || strstr(style_pos, " 0") != NULL) {
                parsed_style = STYLE_STANDARD;
            }
        }

        // Check for slot keyword in string
        const char *slot_pos = strstr(str, "slot");
        if (slot_pos != NULL) {
            const char *sp = slot_pos + 4;
            while (*sp == ':' || *sp == '=' || *sp == ' ' || *sp == '\t') sp++;
            if (*sp >= '0' && *sp <= '9') {
                int val = 0;
                while (*sp >= '0' && *sp <= '9') {
                    val = val * 10 + (*sp - '0');
                    sp++;
                }
                if (val < EPD_SLOT_COUNT) parsed_slot = val;
            }
        }

        // Check compact shorthand formats: "1,2", "1:2", "display:1,2", "set:1,2", "s0".."s13"
        if (parsed_slot < 0) {
            const char *cp = str;
            if (strstr(cp, "display:") == cp) cp += 8;
            else if (strstr(cp, "set:") == cp) cp += 4;
            while (*cp == ' ' || *cp == '\t') cp++;

            if (*cp == 's' && cp[1] >= '0' && cp[1] <= '9') cp++; // allow "s0".."s13"

            if (cp[0] >= '0' && cp[0] <= '9') {
                int val = 0;
                const char *dp = cp;
                while (*dp >= '0' && *dp <= '9') {
                    val = val * 10 + (*dp - '0');
                    dp++;
                }
                if (val < EPD_SLOT_COUNT) {
                    if ((*dp == ',' || *dp == ':' || *dp == ' ' || *dp == '/') &&
                        dp[1] >= '0' && dp[1] <= '4') {
                        parsed_slot = val;
                        if (parsed_style < 0) {
                            parsed_style = dp[1] - '0';
                        }
                    } else if (*dp == '\0' || *dp == ' ' || *dp == '\r' || *dp == '\n' || *dp == ',') {
                        parsed_slot = val;
                    }
                }
            }
        }

        if (parsed_slot >= 0 || parsed_style >= 0) {
            cmd->type = NFC_CMD_SET_DISPLAY;
            cmd->slot = (parsed_slot >= 0) ? (uint8_t)parsed_slot : 0xFF;
            cmd->style = (parsed_style >= 0) ? (uint8_t)parsed_style : 0xFF;
            return true;
        }

        // Any other unrecognized text: cycle to next slot
        cmd->type = NFC_CMD_SET_DISPLAY;
        cmd->slot = (settings.active_slot + 1) % EPD_SLOT_COUNT;
        cmd->style = 0xFF;
        return true;
    }

    // Non-text record written: cycle slot
    cmd->type = NFC_CMD_SET_DISPLAY;
    cmd->slot = (settings.active_slot + 1) % EPD_SLOT_COUNT;
    cmd->style = 0xFF;
    return true;
}

// Finish RF transmission cleanly:
// The FM11NC08 hardware automatically modulates the FIFO contents over the 13.56 MHz
// RF carrier and appends the 16-bit CRC in silicon.
// Once transmission completes, the FM11NC08 RF engine autonomously returns to receive mode.
// We must keep CS HIGH and keep the I2C bus 100% SILENT.
// Crucially, we DO NOT delay, DO NOT write RF_TXEN=0x00, and DO NOT read/clear IRQs here.
// Any post-TX I2C read would wipe out pending RxDone interrupts if a fast reader (like iOS
// CoreNFC with ~1.5 ms turnaround) responds before the delay elapses.
static void nfc_finish_tx_and_prepare_rx(uint8_t tx_len) {
    (void)tx_len;
    last_activity_tick = clock_time();
}

bool nfc_fm11nc08_poll(nfc_cmd_t *cmd) {
    if (cmd) {
        memset(cmd, 0, sizeof(nfc_cmd_t));
    }
    if (!fm11_present) return false;

    // 0. Track PC4 pin level and falling edges
    bool irq_pin = gpio_read(GPIO_NFC_IRQ) ? true : false;
    NFC_DIAG_SET(nfc_diag.irq_pin_level, irq_pin ? 1 : 0);
    if (last_irq_pin_state && !irq_pin) {
        NFC_DIAG_INC(nfc_diag.irq_fall_count);
#if NFC_ENABLE_DIAG
        nfc_uart_printf("[NFC] IRQ fall #%u (PC4 LOW)\n", nfc_diag.irq_fall_count);
#endif
        // Do NOT send BLE notify here to prevent 2.4 GHz radio from interfering with 13.56 MHz NFC reception!
    }
    last_irq_pin_state = irq_pin;

    uint32_t now = clock_time();

    // 1. Process incoming RF frame when IRQ line is asserted (PC4 is LOW on RxDone)
    if (!irq_pin) {
        uint8_t main_irq = 0, fifo_irq = 0, aux_irq = 0;
        nfc_read_irqs(&main_irq, &fifo_irq, &aux_irq);
        NFC_DIAG_SET(nfc_diag.last_main_irq, main_irq);
        NFC_DIAG_SET(nfc_diag.last_fifo_irq, fifo_irq);
        NFC_DIAG_SET(nfc_diag.last_aux_irq,  aux_irq);

        // ONLY process when complete frame has finished arriving in FIFO (RxDone = bit 4)
        if (!(main_irq & 0x10)) {
            if (fifo_irq & 0x04) { // Overflow
                nfc_flush_fifo();
            }
            return false;
        }

        if (!session_active) {
            session_active = true;
            session_start_tick = now;
            write_committed = false;
            write_occurred = false;
            selected_file = T4T_FILE_NONE;
            apdu_processed = false;
            NFC_DIAG_INC(nfc_diag.session_count);
            led_on(LED_GREEN);

            // Always ensure NDEF buffer has fresh telemetry for incoming read
            app_update_nfc_telemetry();
        }
        last_activity_tick = now;

        uint8_t wordcnt = 0;
        nfc_read_reg(FM11_REG_FIFO_WORDCNT, &wordcnt);
        NFC_DIAG_SET(nfc_diag.last_fifo_wordcnt, wordcnt);

        if (wordcnt > 0 && wordcnt <= 32) {
            last_activity_tick = now;
            apdu_processed = true;

            uint8_t rx_buf[32] = {0};
            nfc_read_fifo(rx_buf, wordcnt);
            NFC_DIAG_SET(nfc_diag.last_rx_len, wordcnt);
            NFC_DIAG_MEMCPY(nfc_diag.last_rx_bytes, rx_buf, (wordcnt > 16) ? 16 : wordcnt);

            // Detect ISO 14443-4 framing vs raw APDU
            uint8_t pcb_mode = 0; // 0 = raw APDU, 1 = I-block (no CID), 2 = I-block (with CID)
            uint8_t block_num = 0;
            uint8_t rx_cid = 0;
            const uint8_t *apdu = rx_buf;
            uint8_t apdu_len = wordcnt;

            // Strip 2-byte CRC from received RF frame
            uint8_t frame_len = (wordcnt > 2) ? (wordcnt - 2) : wordcnt;

            if (rx_buf[0] == 0x00 && wordcnt >= 4) {
                // Pure APDU without PCB
                pcb_mode = 0;
                apdu = rx_buf;
                apdu_len = frame_len;
            } else if ((rx_buf[0] & 0xC2) == 0x02) {
                // ISO 14443-4 I-block (b7..6=00, b1=1)
                bool has_cid = (rx_buf[0] & 0x08) != 0;
                block_num = rx_buf[0] & 0x01;

                if (has_cid) {
                    if (frame_len < 1) { nfc_flush_fifo(); return false; }
                    pcb_mode = 2;
                    rx_cid = rx_buf[1];
                    apdu = (frame_len >= 2) ? &rx_buf[2] : NULL;
                    apdu_len = (frame_len >= 2) ? (frame_len - 2) : 0;
                } else {
                    pcb_mode = 1;
                    apdu = (frame_len >= 1) ? &rx_buf[1] : NULL;
                    apdu_len = (frame_len >= 1) ? (frame_len - 1) : 0;
                }

                // Handle Empty I-block (ISO 14443-4 presence check / handshake from reader)
                if (apdu_len == 0) {
                    uint8_t tx_buf[2];
                    uint8_t tx_len = 0;
                    if (pcb_mode == 2) {
                        tx_buf[tx_len++] = 0x0A | block_num;
                        tx_buf[tx_len++] = rx_cid;
                    } else {
                        tx_buf[tx_len++] = 0x02 | block_num;
                    }
                    memcpy(last_tx_buf, tx_buf, tx_len);
                    last_tx_len = tx_len;
                    nfc_write_fifo(tx_buf, tx_len);
                    nfc_write_reg(FM11_REG_RF_TXEN, 0x55);
                    nfc_finish_tx_and_prepare_rx(tx_len);

                    NFC_DIAG_SET(nfc_diag.last_tx_len, tx_len);
                    NFC_DIAG_MEMCPY(nfc_diag.last_tx_bytes, tx_buf, tx_len);
                    return false;
                }

                // If an I-block carries INF data, it must have at least 4 bytes of APDU header (CLA INS P1 P2)
                if (apdu_len < 4) {
                    nfc_flush_fifo();
                    return false;
                }
            } else if ((rx_buf[0] & 0xC7) == 0xC2 && wordcnt >= 1) {
                // S-block DESELECT command (0xC2 or 0xCA)
                uint8_t s_resp[2];
                uint8_t s_len = 0;
                s_resp[s_len++] = rx_buf[0];
                if ((rx_buf[0] & 0x08) && wordcnt >= 2) {
                    s_resp[s_len++] = rx_buf[1];
                }
                nfc_write_fifo(s_resp, s_len);
                nfc_write_reg(FM11_REG_RF_TXEN, 0x55);
                WaitMs(1); // Allow 1-2 byte DESELECT response to transmit cleanly before clearing FIFO

                // Session completed cleanly by reader DESELECT
                session_active = false;
                led_off(LED_GREEN);
                nfc_fm11nc08_clear_irq();
                nfc_write_reg(FM11_REG_MAIN_IRQ_MASK, 0xEF);
                NFC_UPDATE_DIAG_STR();

                if (write_committed || write_occurred) {
                    write_committed = false;
                    write_occurred = false;
                    if (cmd && nfc_parse_written_ndef(cmd)) {
                        app_update_nfc_telemetry();
                        return true;
                    } else {
                        app_update_nfc_telemetry();
                        return false;
                    }
                } else {
                    return false;
                }
            } else if ((rx_buf[0] & 0xE2) == 0xA2 && wordcnt >= 1) {
                // ISO 14443-4 R-block (NAK or ACK: 0xA2, 0xA3, 0xAA, 0xAB, 0xB2, 0xB3, 0xBA, 0xBB)
                bool is_nak = (rx_buf[0] & 0x10) != 0;
                nfc_uart_printf("[NFC] R-%s block (%02X)\n", is_nak ? "NAK" : "ACK", rx_buf[0]);
                if (is_nak && last_tx_len > 0) {
                    // Retransmit cached previous response
                    nfc_uart_printf("[NFC] Retransmitting %uB\n", last_tx_len);
                    nfc_write_fifo(last_tx_buf, last_tx_len);
                    nfc_write_reg(FM11_REG_RF_TXEN, 0x55);
                    nfc_finish_tx_and_prepare_rx(last_tx_len);

#if NFC_ENABLE_DIAG
                    // Record in debug log
                    nfc_debug_log[nfc_debug_idx & 3][0] = rx_buf[0];
                    nfc_debug_log[nfc_debug_idx & 3][1] = 'R';
                    nfc_debug_log[nfc_debug_idx & 3][2] = 'N';
                    nfc_debug_log[nfc_debug_idx & 3][3] = 'A';
                    nfc_debug_log[nfc_debug_idx & 3][4] = 'K';
                    nfc_debug_log[nfc_debug_idx & 3][6] = (last_tx_len >= 2) ? last_tx_buf[last_tx_len - 2] : 0;
                    nfc_debug_log[nfc_debug_idx & 3][7] = (last_tx_len >= 1) ? last_tx_buf[last_tx_len - 1] : 0;
                    nfc_debug_idx++;

                    nfc_diag.apdu_count++;
                    nfc_diag.last_tx_len = last_tx_len;
                    memcpy(nfc_diag.last_tx_bytes, last_tx_buf, (last_tx_len > 8) ? 8 : last_tx_len);
                    nfc_fm11nc08_get_diag_string(nfc_diag_ble_buf, sizeof(nfc_diag_ble_buf));
#endif
                    return false;
                } else {
                    // R(ACK): reader acknowledged receipt of our block
                    nfc_flush_fifo();
                    return false;
                }
            } else {
                // Not a recognized ISO 14443-4 frame (corrupted, collision, or ghost data)
                nfc_uart_printf("[NFC] Dropping unrecognized frame: %02X (len=%u)\n", rx_buf[0], wordcnt);
                nfc_flush_fifo();
                return false;
            }

#if NFC_ENABLE_DIAG
            // Record APDU header in debug buffer
            nfc_debug_log[nfc_debug_idx & 3][0] = rx_buf[0];
            nfc_debug_log[nfc_debug_idx & 3][1] = (apdu_len >= 1) ? apdu[0] : 0;
            nfc_debug_log[nfc_debug_idx & 3][2] = (apdu_len >= 2) ? apdu[1] : 0;
            nfc_debug_log[nfc_debug_idx & 3][3] = (apdu_len >= 3) ? apdu[2] : 0;
            nfc_debug_log[nfc_debug_idx & 3][4] = (apdu_len >= 4) ? apdu[3] : 0;
            nfc_debug_log[nfc_debug_idx & 3][5] = (apdu_len >= 5) ? apdu[4] : 0;
#endif

            // Process ISO 7816-4 APDU
            uint8_t resp_payload[28];
            uint8_t resp_len = 0;
            uint8_t sw1 = 0x6D, sw2 = 0x00;

            if (apdu_len >= 4) {
                uint8_t ins = apdu[1];
                uint8_t p1  = apdu[2];
                uint8_t p2  = apdu[3];

                if (ins == 0xA4) { // SELECT
                    if (p1 == 0x04) { // Select by AID (NFC Forum Application)
                        if (write_committed || write_occurred) {
                            write_committed = false;
                            write_occurred = false;
                            nfc_cmd_t cmd_tmp;
                            if (nfc_parse_written_ndef(&cmd_tmp)) {
                                app_update_nfc_telemetry();
                            } else {
                                app_update_nfc_telemetry();
                            }
                        }
                        selected_file = T4T_FILE_NONE;
                        sw1 = 0x90; sw2 = 0x00;
                    } else { // Select by File ID (P1 == 0x00, 0x01, 0x02, etc.)
                        if (apdu_len >= 7) {
                            uint16_t fid = ((uint16_t)apdu[5] << 8) | apdu[6];
                            if (fid == 0xE103) {
                                selected_file = T4T_FILE_CC;
                                sw1 = 0x90; sw2 = 0x00;
                            } else if (fid == 0xE104) {
                                selected_file = T4T_FILE_NDEF;
                                sw1 = 0x90; sw2 = 0x00;
                            } else {
                                sw1 = 0x6A; sw2 = 0x82; // File not found
                            }
                        } else {
                            sw1 = 0x90; sw2 = 0x00;
                        }
                    }
                } else if (ins == 0xB0) { // READ BINARY
                    uint16_t offset = (((uint16_t)(p1 & 0x7F)) << 8) | p2;
                    uint16_t le = (apdu_len > 4) ? apdu[4] : 0;
                    if (le == 0) le = 256; // Le=00 means 256 bytes in ISO 7816-4

                    // Fallback: If no file selected yet, infer target file
                    if (selected_file == T4T_FILE_NONE) {
                        if (offset < sizeof(t4t_cc_file)) {
                            selected_file = T4T_FILE_CC;
                        } else {
                            selected_file = T4T_FILE_NDEF;
                        }
                    }

                    if (selected_file == T4T_FILE_CC) {
                        if (offset >= sizeof(t4t_cc_file)) {
                            sw1 = 0x6A; sw2 = 0x86; // Wrong parameters P1-P2
                        } else {
                            uint16_t avail = sizeof(t4t_cc_file) - offset;
                            if (le > avail) le = avail;
                            if (le > 26) le = 26; // Cap to MLe
                            memcpy(resp_payload, &t4t_cc_file[offset], le);
                            resp_len = (uint8_t)le;
                            sw1 = 0x90; sw2 = 0x00;
                        }
                    } else if (selected_file == T4T_FILE_NDEF) {
                        if (offset == 0 && !write_occurred) {
                            app_update_nfc_telemetry();
                        }
                        uint16_t file_len = ((uint16_t)t4t_ndef_file[0] << 8) | t4t_ndef_file[1];
                        uint16_t total_active = file_len + 2;
                        if (total_active > sizeof(t4t_ndef_file)) total_active = sizeof(t4t_ndef_file);

                        if (offset >= total_active) {
                            sw1 = 0x6A; sw2 = 0x86; // Offset beyond file size
                        } else {
                            uint16_t avail = total_active - offset;
                            if (le > avail) le = avail;
                            if (le > 26) le = 26; // Cap to MLe
                            memcpy(resp_payload, &t4t_ndef_file[offset], le);
                            resp_len = (uint8_t)le;
                            sw1 = 0x90; sw2 = 0x00;
                        }
                    } else {
                        sw1 = 0x69; sw2 = 0x86; // Command not allowed
                    }
                } else if (ins == 0xD6) { // UPDATE BINARY
                    uint16_t offset = (((uint16_t)(p1 & 0x7F)) << 8) | p2;
                    uint8_t lc = (apdu_len > 4) ? apdu[4] : 0;
                    NFC_DIAG_INC(nfc_diag.write_count);
                    nfc_uart_printf("[NFC] UPDATE BINARY off=%u lc=%u\n", offset, lc);

                    // On Type 4 Tag, NDEF is the only writable file
                    selected_file = T4T_FILE_NDEF;

                    if (offset + lc > sizeof(t4t_ndef_file)) {
                        sw1 = 0x6A; sw2 = 0x84; // Not enough memory
                    } else {
                        uint8_t copy_len = lc;
                        if (5 + copy_len > apdu_len) {
                            copy_len = (apdu_len > 5) ? (apdu_len - 5) : 0;
                        }
                        if (copy_len > 0) {
                            memcpy(&t4t_ndef_file[offset], &apdu[5], copy_len);
                            write_occurred = true;
                            if (offset == 0 && copy_len >= 2) {
                                uint16_t written_nlen = ((uint16_t)apdu[5] << 8) | apdu[6];
                                if (written_nlen > 0) {
                                    write_committed = true;
                                }
                            }
                        }
                        sw1 = 0x90; sw2 = 0x00;
                    }
                }
            }

#if NFC_ENABLE_DIAG
            // Record SW in debug log
            nfc_debug_log[nfc_debug_idx & 3][6] = sw1;
            nfc_debug_log[nfc_debug_idx & 3][7] = sw2;
            nfc_debug_idx++;
#endif

            // Assemble response frame
            uint8_t tx_buf[32];
            uint8_t tx_len = 0;
            if (pcb_mode == 1) {
                tx_buf[tx_len++] = 0x02 | block_num;
            } else if (pcb_mode == 2) {
                tx_buf[tx_len++] = 0x0A | block_num;
                tx_buf[tx_len++] = rx_cid;
            }
            if (resp_len > 0) {
                memcpy(&tx_buf[tx_len], resp_payload, resp_len);
                tx_len += resp_len;
            }
            tx_buf[tx_len++] = sw1;
            tx_buf[tx_len++] = sw2;

            // Cache for potential R(NAK) retransmission
            memcpy(last_tx_buf, tx_buf, tx_len);
            last_tx_len = tx_len;

            // FAST TRANSMISSION: Push to FIFO and trigger RF load modulation IMMEDIATELY
            nfc_write_fifo(tx_buf, tx_len);
            nfc_write_reg(FM11_REG_RF_TXEN, 0x55);
            nfc_finish_tx_and_prepare_rx(tx_len);

#if NFC_ENABLE_DIAG
            // POST-TX: Now that the response is safely over the air, record diagnostics
            nfc_diag.apdu_count++;
            nfc_diag.last_tx_len = tx_len;
            memcpy(nfc_diag.last_tx_bytes, tx_buf, (tx_len > 8) ? 8 : tx_len);
            nfc_fm11nc08_get_diag_string(nfc_diag_ble_buf, sizeof(nfc_diag_ble_buf));
            nfc_uart_printf("[NFC] RX(%uB): %02X %02X %02X %02X -> TX(%uB): %02X %02X SW=%02X%02X RF=%02X\n",
                            wordcnt, rx_buf[0], rx_buf[1], rx_buf[2], rx_buf[3],
                            tx_len, tx_buf[0], tx_buf[1], sw1, sw2, nfc_diag.rf_status_reg);
#else
            nfc_uart_printf("[NFC] RX(%uB) -> TX(%uB) SW=%02X%02X\n", wordcnt, tx_len, sw1, sw2);
#endif
        } else {
            // Wordcnt was 0 or invalid: flush FIFO
            nfc_flush_fifo();
        }
    } else {
        // While waiting for incoming RF data from smartphone, keep I2C bus completely silent!
        WaitUs(500);
    }

    // 2. Session completion evaluation (ONLY when session is currently active!)
    // Close session if:
    // a. Inactivity between APDUs for 400 ms (phone removed or transaction completed), OR
    // b. Overall session safety timeout (10 seconds)
    if (session_active && (clock_time_exceed(last_activity_tick, 400 * 1000) ||
                           clock_time_exceed(session_start_tick, 10000 * 1000))) {

        session_active = false;
        led_off(LED_GREEN);

#if NFC_ENABLE_DIAG
        nfc_uart_printf("[NFC] Session end (dur=%u ms, apdu=%u, wr=%u)\n",
                        (clock_time() - session_start_tick) / (CLOCK_SYS_CLOCK_HZ / 1000),
                        nfc_diag.apdu_count, nfc_diag.write_count);
        nfc_fm11nc08_get_diag_string(nfc_diag_ble_buf, sizeof(nfc_diag_ble_buf));
#else
        nfc_uart_printf("[NFC] Session end (dur=%u ms)\n",
                        (clock_time() - session_start_tick) / (CLOCK_SYS_CLOCK_HZ / 1000));
#endif

        // Clear all IRQ flags and flush FIFO
        nfc_fm11nc08_clear_irq();
        nfc_write_reg(FM11_REG_MAIN_IRQ_MASK, 0xEF);

        if (write_committed || write_occurred) {
            write_committed = false;
            write_occurred = false;
            if (cmd && nfc_parse_written_ndef(cmd)) {
                app_update_nfc_telemetry();
                return true;
            } else {
                app_update_nfc_telemetry();
                return false;
            }
        } else {
            return false;
        }
    }

    return false;
}

#include "mode_switch.h"

void nfc_process_events(void) {
    nfc_cmd_t cmd;
    if (nfc_fm11nc08_poll(&cmd)) {
        switch (cmd.type) {
            case NFC_CMD_ZIGBEE_RESET:
                mode_switch_zigbee_reset();
                break;
            case NFC_CMD_SWITCH_ZIGBEE:
                mode_switch_to(DEVICE_MODE_ZIGBEE);
                break;
            case NFC_CMD_SWITCH_BLE:
                mode_switch_to(DEVICE_MODE_BLE);
                break;
            case NFC_CMD_SET_DISPLAY: {
                bool changed = false;
                if (cmd.slot < EPD_SLOT_COUNT && cmd.slot != settings.active_slot) {
                    settings.active_slot = cmd.slot;
                    changed = true;
                }
                if (cmd.style < STYLE_COUNT && cmd.style != settings.render_style) {
                    settings.render_style = cmd.style;
                    changed = true;
                }
                if (changed || (cmd.slot < EPD_SLOT_COUNT) || (cmd.style < STYLE_COUNT)) {
                    flash_eep_save();
                    epd_display_slot(settings.active_slot, settings.render_style);
                    app_update_nfc_telemetry();
                    if (settings.active_mode == DEVICE_MODE_ZIGBEE) {
                        g_zcl_epaperAttrs.activeSlot = settings.active_slot;
                        g_zcl_epaperAttrs.renderStyle = settings.render_style;
                        g_zcl_epaperAttrs.activeMode = settings.active_mode;
                        g_zcl_epaperAttrs.refreshCount = settings.screen_refresh_count;
                        if (zb_isDeviceJoinedNwk()) {
                            zb_epaper_report_attrs();
                        } else if (!zb_isDeviceFactoryNew()) {
                            zb_start_rejoin();
                        }
                    }
                }
                break;
            }
            default:
                break;
        }
    } else {
        // If device was woken by NFC read and parent is lost (ZB-X), initiate rejoin
        if (settings.active_mode == DEVICE_MODE_ZIGBEE && !zb_isDeviceJoinedNwk() && !zb_isDeviceFactoryNew()) {
            static uint32_t last_nfc_read_rejoin_tick = 0;
            if (clock_time_exceed(last_nfc_read_rejoin_tick, 5 * 1000 * 1000)) {
                last_nfc_read_rejoin_tick = clock_time();
                zb_start_rejoin();
            }
        }
    }
}

