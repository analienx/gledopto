#ifndef GLSD301P_TELINK_PIN_CONTRACT_H
#define GLSD301P_TELINK_PIN_CONTRACT_H

/*
 * Post-SDK GPIO contract.
 *
 * app_cfg.h is parsed by Telink before gpio.h has defined GPIO_PinTypeDef, so
 * enum-level checks cannot safely live in the early target contract. Include
 * this header only from a translation unit after tl_common.h + drv_gpio.h.
 *
 * The pinned TC32 compiler predates C11; negative-size typedefs provide
 * compile-time assertions without requiring _Static_assert.
 */

typedef char glsd301p_adc_pin_must_be_pb3[
    (VOLTAGE_DETECT_ADC_PIN == GPIO_PB3) ? 1 : -1];
typedef char glsd301p_sdk_pb3_encoding_must_match_contract[
    (GPIO_PB3 == GLSD301P_TARGET_ADC_FLASH_SAFETY_GPIO_ENCODED) ? 1 : -1];
typedef char glsd301p_sdk_pb1_encoding_must_match_contract[
    (GPIO_PB1 == GLSD301P_TARGET_UART_TX_GPIO_ENCODED) ? 1 : -1];
typedef char glsd301p_sdk_pa0_encoding_must_match_contract[
    (GPIO_PA0 == GLSD301P_TARGET_UART_RX_GPIO_ENCODED) ? 1 : -1];
typedef char glsd301p_sdk_pc2_encoding_must_match_contract[
    (GPIO_PC2 == GLSD301P_TARGET_PUSH_GPIO_ENCODED) ? 1 : -1];
typedef char glsd301p_sdk_pb4_encoding_must_match_contract[
    (GPIO_PB4 == GLSD301P_TARGET_AUX_INPUT_GPIO_ENCODED) ? 1 : -1];

typedef char glsd301p_adc_pin_not_uart_tx[(GPIO_PB3 != GPIO_PB1) ? 1 : -1];
typedef char glsd301p_adc_pin_not_uart_rx[(GPIO_PB3 != GPIO_PA0) ? 1 : -1];
typedef char glsd301p_adc_pin_not_push[(GPIO_PB3 != GPIO_PC2) ? 1 : -1];
typedef char glsd301p_adc_pin_not_aux_input[(GPIO_PB3 != GPIO_PB4) ? 1 : -1];

#endif /* GLSD301P_TELINK_PIN_CONTRACT_H */
