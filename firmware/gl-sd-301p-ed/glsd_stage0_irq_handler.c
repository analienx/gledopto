/*
 * Minimal TLSR8258 IRQ dispatcher for the Stage-0 safety canary.
 *
 * The normal Telink generic dispatcher always references UART DMA handlers,
 * even when the application never initializes UART.  Stage-0 deliberately
 * removes that capability at link time, so this dispatcher handles only the
 * RF and timer sources needed by the Zigbee stack/event loop.
 */

#ifdef GLSD_TELINK_SDK

#include "tl_common.h"

extern void rf_rx_irq_handler(void);
extern void rf_tx_irq_handler(void);

_attribute_ram_code_ void irq_handler(void)
{
    u16 src_rf = rf_irq_src_get();
    u32 src;

    if (src_rf & FLD_RF_IRQ_TX) {
        rf_irq_clr_src(FLD_RF_IRQ_TX);
        rf_tx_irq_handler();
    }

    if (src_rf & FLD_RF_IRQ_RX) {
        rf_irq_clr_src(FLD_RF_IRQ_RX);
        rf_rx_irq_handler();
    }

    if (src_rf & FLD_RF_IRQ_RX_TIMEOUT) {
        rf_irq_clr_src(FLD_RF_IRQ_RX_TIMEOUT);
    }

    if (src_rf & FLD_RF_IRQ_FIRST_TIMEOUT) {
        rf_irq_clr_src(FLD_RF_IRQ_FIRST_TIMEOUT);
    }

    src = irq_get_src();

    if (src & FLD_IRQ_TMR0_EN) {
        reg_irq_src = FLD_IRQ_TMR0_EN;
        reg_tmr_sta = FLD_TMR_STA_TMR0;
        drv_timer_irq0_handler();
    }

    if (src & FLD_IRQ_TMR1_EN) {
        reg_irq_src = FLD_IRQ_TMR1_EN;
        reg_tmr_sta = FLD_TMR_STA_TMR1;
        drv_timer_irq1_handler();
    }

    if (src & FLD_IRQ_SYSTEM_TIMER) {
        reg_irq_src = FLD_IRQ_SYSTEM_TIMER;
        drv_timer_irq3_handler();
    }

    /* No UART DMA or local-input GPIO IRQ handling exists in Stage-0. */
}

#endif /* GLSD_TELINK_SDK */
