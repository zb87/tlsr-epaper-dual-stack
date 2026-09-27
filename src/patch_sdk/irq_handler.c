/********************************************************************************************************
 * @file    irq_handler.c
 *
 * @brief   Custom IRQ handler for TLSR8258 Dual-Stack ESL firmware.
 *          Dispatches to BLE LL handler in BLE mode, or to standard Telink Zigbee
 *          MAC/RF/Timer handlers in Zigbee mode.
 *          CRITICAL: Does NOT perform runtime dual-mode timeslicing into uninitialized BLE stack.
 *******************************************************************************************************/

#include "tl_common.h"
#include "zigbee_ble_switch.h"
#include "app_config.h"

extern void rf_rx_irq_handler(void);
extern void rf_tx_irq_handler(void);
extern void irq_blt_sdk_handler(void);
extern void drv_timer_irq0_handler(void);
extern void drv_timer_irq1_handler(void);
extern void drv_timer_irq3_handler(void);
extern void drv_gpio_irq_handler(void);
extern void drv_gpio_irq_risc0_handler(void);
extern void drv_gpio_irq_risc1_handler(void);
extern void drv_uart_rx_irq_handler(void);
extern void drv_uart_tx_irq_handler(void);

volatile u8 T_DBG_irqTest[16] = {0};

_attribute_ram_code_ void irq_handler(void) {
    if (CURRENT_SLOT_GET() == DUALMODE_SLOT_BLE) {
        irq_blt_sdk_handler();

        u32 src = irq_get_src();
        if ((src & FLD_IRQ_GPIO_EN) == FLD_IRQ_GPIO_EN) {
            reg_irq_src = FLD_IRQ_GPIO_EN;
            drv_gpio_irq_handler();
        }
        if ((src & FLD_IRQ_GPIO_RISC0_EN) == FLD_IRQ_GPIO_RISC0_EN) {
            reg_irq_src = FLD_IRQ_GPIO_RISC0_EN;
            drv_gpio_irq_risc0_handler();
        }
        if ((src & FLD_IRQ_GPIO_RISC1_EN) == FLD_IRQ_GPIO_RISC1_EN) {
            reg_irq_src = FLD_IRQ_GPIO_RISC1_EN;
            drv_gpio_irq_risc1_handler();
        }
        u16 dma_irq_source = dma_chn_irq_status_get();
        if (dma_irq_source & FLD_DMA_CHN_UART_RX) {
            dma_chn_irq_status_clr(FLD_DMA_CHN_UART_RX);
            drv_uart_rx_irq_handler();
        } else if (dma_irq_source & FLD_DMA_CHN_UART_TX) {
            dma_chn_irq_status_clr(FLD_DMA_CHN_UART_TX);
            drv_uart_tx_irq_handler();
        } else {
            dma_chn_irq_status_clr(~(FLD_DMA_CHN_UART_TX | FLD_DMA_CHN_UART_RX));
        }
    } else {
        // Zigbee Mode Interrupt Dispatcher (matches Telink Zigbee SDK)
        u16 src_rf = rf_irq_src_get();
        if (src_rf & FLD_RF_IRQ_TX) {
            rf_irq_clr_src(FLD_RF_IRQ_TX);
            T_DBG_irqTest[0]++;
            rf_tx_irq_handler();
        }
        if (src_rf & FLD_RF_IRQ_RX) {
            rf_irq_clr_src(FLD_RF_IRQ_RX);
            T_DBG_irqTest[1]++;
            rf_rx_irq_handler();
        }
        if (src_rf & FLD_RF_IRQ_RX_TIMEOUT) {
            rf_irq_clr_src(FLD_RF_IRQ_RX_TIMEOUT);
        }
        if (src_rf & FLD_RF_IRQ_FIRST_TIMEOUT) {
            rf_irq_clr_src(FLD_RF_IRQ_FIRST_TIMEOUT);
        }

        u32 src = irq_get_src();

        if (src & FLD_IRQ_TMR0_EN) {
            reg_irq_src = FLD_IRQ_TMR0_EN;
            reg_tmr_sta = FLD_TMR_STA_TMR0;
            T_DBG_irqTest[2]++;
            drv_timer_irq0_handler();
        }
        if (src & FLD_IRQ_TMR1_EN) {
            reg_irq_src = FLD_IRQ_TMR1_EN;
            reg_tmr_sta = FLD_TMR_STA_TMR1;
            T_DBG_irqTest[3]++;
            drv_timer_irq1_handler();
        }
        if (src & FLD_IRQ_SYSTEM_TIMER) {
            reg_irq_src = FLD_IRQ_SYSTEM_TIMER;
            T_DBG_irqTest[4]++;
            drv_timer_irq3_handler();
        }
        if ((src & FLD_IRQ_GPIO_EN) == FLD_IRQ_GPIO_EN) {
            reg_irq_src = FLD_IRQ_GPIO_EN;
            T_DBG_irqTest[5]++;
            drv_gpio_irq_handler();
        }
        if ((src & FLD_IRQ_GPIO_RISC0_EN) == FLD_IRQ_GPIO_RISC0_EN) {
            reg_irq_src = FLD_IRQ_GPIO_RISC0_EN;
            T_DBG_irqTest[6]++;
            drv_gpio_irq_risc0_handler();
        }
        if ((src & FLD_IRQ_GPIO_RISC1_EN) == FLD_IRQ_GPIO_RISC1_EN) {
            reg_irq_src = FLD_IRQ_GPIO_RISC1_EN;
            T_DBG_irqTest[7]++;
            drv_gpio_irq_risc1_handler();
        }

        u16 dma_irq_source = dma_chn_irq_status_get();
        if (dma_irq_source & FLD_DMA_CHN_UART_RX) {
            dma_chn_irq_status_clr(FLD_DMA_CHN_UART_RX);
            T_DBG_irqTest[8]++;
            drv_uart_rx_irq_handler();
        } else if (dma_irq_source & FLD_DMA_CHN_UART_TX) {
            dma_chn_irq_status_clr(FLD_DMA_CHN_UART_TX);
            T_DBG_irqTest[9]++;
            drv_uart_tx_irq_handler();
        } else {
            dma_chn_irq_status_clr(~(FLD_DMA_CHN_UART_TX | FLD_DMA_CHN_UART_RX));
        }
    }
}
