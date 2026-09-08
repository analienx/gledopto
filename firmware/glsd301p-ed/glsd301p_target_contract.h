#ifndef GLSD301P_TARGET_CONTRACT_H
#define GLSD301P_TARGET_CONTRACT_H

/*
 * Compile-time architecture firewall for the production GL-SD-301P target.
 *
 * Include this only after the Telink application/stack configuration has
 * defined the selected role and feature macros. A production target is not
 * allowed to compile if it drifts back toward Router/coordinator behavior,
 * sleepy End Device operation, loses required interoperability capabilities,
 * or reuses the vendor-confirmed ADC flash-safety pin for another function.
 */

#define GLSD301P_TARGET_ENDPOINT                         0x0Bu
#define GLSD301P_TARGET_POWER_SOURCE_MAINS               0x01u
#define GLSD301P_TARGET_UART_BAUD                         9600u
#define GLSD301P_TARGET_UART_TX_GPIO_ENCODED              0x0102u /* PB1 */
#define GLSD301P_TARGET_UART_RX_GPIO_ENCODED              0x0001u /* PA0 */
#define GLSD301P_TARGET_ADC_FLASH_SAFETY_GPIO_ENCODED     0x0108u /* PB3 */
#define GLSD301P_TARGET_PUSH_GPIO_ENCODED                 0x0204u /* PC2 */
#define GLSD301P_TARGET_AUX_INPUT_GPIO_ENCODED            0x0110u /* PB4 */

#if !defined(MCU_CORE_8258) || !(MCU_CORE_8258)
#error "GL-SD-301P target requires MCU_CORE_8258"
#endif

#if !defined(END_DEVICE) || !(END_DEVICE)
#error "GL-SD-301P target must be built as END_DEVICE"
#endif

#if defined(ROUTER) && (ROUTER)
#error "GL-SD-301P target must never be built as ROUTER"
#endif

#if defined(COORDINATOR) && (COORDINATOR)
#error "GL-SD-301P target must never be built as COORDINATOR"
#endif

#if !defined(ZB_ED_ROLE) || !(ZB_ED_ROLE)
#error "Telink stack configuration must resolve ZB_ED_ROLE=1"
#endif

#if defined(ZB_ROUTER_ROLE) && (ZB_ROUTER_ROLE)
#error "Telink stack configuration must resolve ZB_ROUTER_ROLE=0/undefined"
#endif

#if defined(ZB_COORDINATOR_ROLE) && (ZB_COORDINATOR_ROLE)
#error "Telink stack configuration must resolve ZB_COORDINATOR_ROLE=0/undefined"
#endif

#if !defined(PM_ENABLE) || (PM_ENABLE)
#error "GL-SD-301P is mains powered; PM_ENABLE must be 0"
#endif

#if !defined(ZB_MAC_RX_ON_WHEN_IDLE) || !(ZB_MAC_RX_ON_WHEN_IDLE)
#error "GL-SD-301P End Device must remain RX-on-when-idle"
#endif

#if !defined(ZCL_ON_OFF_SUPPORT) || !(ZCL_ON_OFF_SUPPORT)
#error "GL-SD-301P target requires ZCL OnOff server support"
#endif

#if !defined(ZCL_LEVEL_CTRL_SUPPORT) || !(ZCL_LEVEL_CTRL_SUPPORT)
#error "GL-SD-301P target requires ZCL Level Control support"
#endif

#if !defined(ZCL_GROUP_SUPPORT) || !(ZCL_GROUP_SUPPORT)
#error "GL-SD-301P target requires APS/ZCL group-addressed control"
#endif

#if !defined(APS_GROUP_TABLE_NUM) || ((APS_GROUP_TABLE_NUM) < 1)
#error "GL-SD-301P target requires a non-empty APS group table"
#endif

#if defined(GLSD301P_ENDPOINT) && ((GLSD301P_ENDPOINT) != GLSD301P_TARGET_ENDPOINT)
#error "GL-SD-301P interoperability endpoint is fixed at 11"
#endif

/*
 * The real TC32 target enables these checks only after Telink gpio.h has
 * supplied the GPIO enum constants. They deliberately use C99-compatible
 * negative-array assertions because the pinned TC32 compiler predates C11.
 */
#if defined(GLSD301P_TARGET_ENABLE_SDK_PIN_ASSERTS) && GLSD301P_TARGET_ENABLE_SDK_PIN_ASSERTS
typedef char glsd301p_adc_pin_must_be_pb3[
    (VOLTAGE_DETECT_ADC_PIN == GPIO_PB3) ? 1 : -1];
typedef char glsd301p_adc_pin_not_uart_tx[
    (GPIO_PB3 != GPIO_PB1) ? 1 : -1];
typedef char glsd301p_adc_pin_not_uart_rx[
    (GPIO_PB3 != GPIO_PA0) ? 1 : -1];
typedef char glsd301p_adc_pin_not_push[
    (GPIO_PB3 != GPIO_PC2) ? 1 : -1];
typedef char glsd301p_adc_pin_not_aux_input[
    (GPIO_PB3 != GPIO_PB4) ? 1 : -1];
#endif

#endif /* GLSD301P_TARGET_CONTRACT_H */
