#ifndef _NFC_FM11NC08_H_
#define _NFC_FM11NC08_H_

#include <stdint.h>
#include <stdbool.h>

// FM11NC08 I2C Slave Addresses (8-bit)
#define FM11NC08_I2C_ADDR_W     0xAE
#define FM11NC08_I2C_ADDR_R     0xAF

// System Register Addresses (Accessed via 16-bit address 0xFFxx)
#define FM11_REG_USER_CFG0      0xFFE0
#define FM11_REG_USER_CFG1      0xFFE1
#define FM11_REG_USER_CFG2      0xFFE2
#define FM11_REG_RESET_SILENCE  0xFFE6
#define FM11_REG_STATUS         0xFFE7
#define FM11_REG_VOUT_EN_CFG    0xFFE9
#define FM11_REG_VOUT_RES_CFG   0xFFEA
#define FM11_REG_FIFO_ACCESS    0xFFF0
#define FM11_REG_FIFO_CLEAR     0xFFF1
#define FM11_REG_FIFO_WORDCNT   0xFFF2
#define FM11_REG_RF_STATUS      0xFFF3
#define FM11_REG_RF_TXEN        0xFFF4
#define FM11_REG_RF_CFG         0xFFF5
#define FM11_REG_RF_RATS        0xFFF6
#define FM11_REG_MAIN_IRQ       0xFFF7
#define FM11_REG_FIFO_IRQ       0xFFF8
#define FM11_REG_AUX_IRQ        0xFFF9
#define FM11_REG_MAIN_IRQ_MASK  0xFFFA
#define FM11_REG_FIFO_IRQ_MASK  0xFFFB
#define FM11_REG_AUX_IRQ_MASK   0xFFFC

// EEPROM Block Addresses
#define FM11_EEPROM_USER_CFG    0x0390 // Block E4 (USER_CFG0, USER_CFG1, USER_CFG2, CHK)
#define FM11_EEPROM_USER_DEF    0x0394 // Block E5 (USER_CFG Defaults)
#define FM11_EEPROM_ATS_TL_T0   0x03B0 // Block EC (+1=TL, +2=T0, +3=I2C Address)
#define FM11_EEPROM_ATS_TA_TC   0x03B4 // Block ED (+0=TA, +1=TB, +2=TC)

#ifndef NFC_ENABLE_DIAG
#define NFC_ENABLE_DIAG 0
#endif

#if NFC_ENABLE_DIAG
// NFC Diagnostic Data Snapshot
typedef struct {
    uint8_t  present;           // 1 if chip ACKed I2C, 0 otherwise
    uint8_t  i2c_addr;          // 0xAE, 0xA0, etc.
    uint8_t  vendor_id;         // byte at EEPROM 0x0000 (0x1D = Fudan)
    uint8_t  irq_pin_level;     // current GPIO level of PC4 (0=LOW, 1=HIGH)
    uint16_t irq_fall_count;    // number of times PC4 dropped LOW
    uint8_t  user_cfg[4];       // readback of 0x0390 (CFG0, CFG1, CFG2, CHK)
    uint8_t  ats_tl_t0[2];      // readback of 0x03B0 (TL, T0)
    uint8_t  ats_ta_tc[3];      // readback of 0x03B4 (TA, TB, TC)
    uint8_t  reg_user_cfg0;     // register 0xFFE0
    uint8_t  status_reg;        // register 0xFFE7 (STATUS, bit 0: user_cfg_chk_flag)
    uint8_t  rf_status_reg;     // register 0xFFF3 (RF_STATUS)
    uint8_t  reg_main_irq_mask; // register 0xFFFA
    uint8_t  last_main_irq;     // last read MAIN_IRQ value
    uint8_t  last_fifo_irq;     // last read FIFO_IRQ value
    uint8_t  last_aux_irq;      // last read AUX_IRQ value
    uint8_t  last_fifo_wordcnt; // last read FIFO_WORDCNT value
    uint8_t  last_rx_len;       // length of last received APDU
    uint8_t  last_rx_bytes[16]; // first 16 bytes of last received APDU
    uint8_t  last_tx_len;       // length of last transmitted response
    uint8_t  last_tx_bytes[8];  // first 8 bytes of last transmitted response
    uint16_t session_count;     // number of sessions entered
    uint16_t apdu_count;        // total APDUs processed
    uint16_t write_count;       // total writes processed
} nfc_diag_data_t;

void nfc_fm11nc08_get_diag(nfc_diag_data_t *dst);
int nfc_fm11nc08_get_diag_string(char *dst, int max_len);
#else
typedef struct { uint8_t dummy; } nfc_diag_data_t;
static inline void nfc_fm11nc08_get_diag(nfc_diag_data_t *dst) { (void)dst; }
static inline int nfc_fm11nc08_get_diag_string(char *dst, int max_len) { (void)dst; (void)max_len; return 0; }
#endif

// Command types parsed from incoming NDEF text messages
typedef enum {
    NFC_CMD_NONE = 0,
    NFC_CMD_SET_DISPLAY,    // update slot and/or style
    NFC_CMD_SWITCH_ZIGBEE,  // switch to Zigbee mode
    NFC_CMD_SWITCH_BLE,     // switch to BLE mode
    NFC_CMD_ZIGBEE_RESET,   // reset Zigbee network and enter pairing mode
} nfc_cmd_type_t;

typedef struct {
    nfc_cmd_type_t type;
    uint8_t slot;   // 0..2, or 0xFF if unchanged
    uint8_t style;  // 0..3, or 0xFF if unchanged
} nfc_cmd_t;

bool nfc_fm11nc08_init(bool is_cold_boot);
void nfc_fm11nc08_clear_irq(void);
bool nfc_fm11nc08_update_telemetry(uint8_t mode, uint8_t slot_idx, uint8_t style, uint16_t vbat_mv, uint8_t temp_c, uint16_t refresh_count);
bool nfc_fm11nc08_poll(nfc_cmd_t *cmd);
bool nfc_fm11nc08_is_active(void);
bool nfc_fm11nc08_is_present(void);
void app_update_nfc_telemetry(void);
void nfc_process_events(void);

#endif // _NFC_FM11NC08_H_
