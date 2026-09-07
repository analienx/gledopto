#include "glsd301p_target_contract.h"

_Static_assert(GLSD301P_TARGET_ENDPOINT == 0x0Bu, "endpoint 11 must stay fixed");
_Static_assert(GLSD301P_TARGET_POWER_SOURCE_MAINS == 0x01u, "mains power source required");
_Static_assert(GLSD301P_TARGET_UART_BAUD == 9600u, "power-stage UART baud drift");
_Static_assert(GLSD301P_TARGET_UART_TX_GPIO_ENCODED == 0x0102u, "PB1 UART TX drift");
_Static_assert(GLSD301P_TARGET_UART_RX_GPIO_ENCODED == 0x0001u, "PA0 UART RX drift");

int main(void)
{
    return 0;
}
