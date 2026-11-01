#include "glsd301p_target_contract.h"

_Static_assert(GLSD301P_TARGET_ENDPOINT == 0x0Bu, "endpoint 11 must stay fixed");
_Static_assert(GLSD301P_TARGET_POWER_SOURCE_MAINS == 0x01u, "mains power source required");
_Static_assert(GLSD301P_TARGET_UART_BAUD == 9600u, "power-stage UART baud drift");
_Static_assert(GLSD301P_TARGET_UART_TX_GPIO_ENCODED == 0x0102u, "PB1 UART TX drift");
_Static_assert(GLSD301P_TARGET_UART_RX_GPIO_ENCODED == 0x0001u, "PA0 UART RX drift");
_Static_assert(GLSD301P_TARGET_ADC_FLASH_SAFETY_GPIO_ENCODED == 0x0108u, "PB3 ADC flash-safety pin drift");
_Static_assert(GLSD301P_TARGET_PUSH_GPIO_ENCODED == 0x0204u, "PC2 PUSH pin drift");
_Static_assert(GLSD301P_TARGET_AUX_INPUT_GPIO_ENCODED == 0x0110u, "PB4 auxiliary input drift");

_Static_assert(GLSD301P_TARGET_ADC_FLASH_SAFETY_GPIO_ENCODED != GLSD301P_TARGET_UART_TX_GPIO_ENCODED,
               "ADC safety pin collides with UART TX");
_Static_assert(GLSD301P_TARGET_ADC_FLASH_SAFETY_GPIO_ENCODED != GLSD301P_TARGET_UART_RX_GPIO_ENCODED,
               "ADC safety pin collides with UART RX");
_Static_assert(GLSD301P_TARGET_ADC_FLASH_SAFETY_GPIO_ENCODED != GLSD301P_TARGET_PUSH_GPIO_ENCODED,
               "ADC safety pin collides with PUSH input");
_Static_assert(GLSD301P_TARGET_ADC_FLASH_SAFETY_GPIO_ENCODED != GLSD301P_TARGET_AUX_INPUT_GPIO_ENCODED,
               "ADC safety pin collides with auxiliary input");

int main(void)
{
    return 0;
}
