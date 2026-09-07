#include "glsd_power_stage.h"

#ifdef GLSD_TELINK_SDK

#include "tl_common.h"
#include "drv_uart.h"
#include "drv_gpio.h"

#include "glsd301p_power_stage_policy.h"
#include "glsd301p_pb4_compat.h"
#include "glsd301p_uart_frame.h"

#define GLSD_UART_BAUD          9600u
#define GLSD_UART_RX_BYTES      64u
#define GLSD_PHYSICAL_MIN_LEVEL 2u

static u8 g_uart_rx[GLSD_UART_RX_BYTES];
static u8 g_initialized;

static void glsd_uart_rx_noop(void)
{
    /* Stock analyzed application installs an effectively no-op receive
     * callback. Keep RX configured for electrical/driver compatibility while
     * deliberately making no protocol assumption about incoming bytes. */
}

static int glsd_send(const uint8_t frame[GLSD301P_CONTROL_FRAME_SIZE])
{
    if (!g_initialized || frame == NULL) {
        return -1;
    }

    return drv_uart_tx_start((u8 *)frame, GLSD301P_CONTROL_FRAME_SIZE) == 1u ? 0 : -1;
}

int glsd_power_stage_init(void)
{
    drv_uart_pin_set(GPIO_PB1, GPIO_PA0);
    if (drv_uart_init(GLSD_UART_BAUD,
                      g_uart_rx,
                      GLSD_UART_RX_BYTES,
                      glsd_uart_rx_noop) != 0u) {
        return -1;
    }

    /* Preserve stock GPIO setup identified from the GL-SD-301P image. */
    drv_gpio_func_set(GPIO_PC2);
    drv_gpio_output_en(GPIO_PC2, 0);
    drv_gpio_input_en(GPIO_PC2, 1);
    drv_gpio_up_down_resistor(GPIO_PC2, PM_PIN_PULLUP_10K);

    drv_gpio_func_set(GPIO_PB4);
    drv_gpio_output_en(GPIO_PB4, 0);
    drv_gpio_input_en(GPIO_PB4, 1);
    drv_gpio_up_down_resistor(GPIO_PB4, PM_PIN_PULLDOWN_100K);

    g_initialized = 1u;
    return 0;
}

int glsd_power_stage_apply(uint8_t on, uint8_t level)
{
    uint8_t frame[GLSD301P_CONTROL_FRAME_SIZE];

    if (!glsd301p_normal_output_frame_encode(on != 0u,
                                              level,
                                              GLSD_PHYSICAL_MIN_LEVEL,
                                              false,
                                              frame)) {
        return -1;
    }

    return glsd_send(frame);
}

int glsd_power_stage_apply_pb4_aux(uint8_t logical_level)
{
    uint8_t frame[GLSD301P_CONTROL_FRAME_SIZE];
    const uint8_t physical_request = glsd301p_pb4_compat_level(logical_level);

    if (!glsd301p_control_frame_encode(GLSD301P_CONTROL_FAMILY_LEVEL,
                                       physical_request,
                                       frame)) {
        return -1;
    }

    return glsd_send(frame);
}

int glsd_power_stage_pc2_high(void)
{
    return drv_gpio_read(GPIO_PC2) ? 1 : 0;
}

int glsd_power_stage_pb4_high(void)
{
    return drv_gpio_read(GPIO_PB4) ? 1 : 0;
}

#else

int glsd_power_stage_init(void) { return -1; }
int glsd_power_stage_apply(uint8_t on, uint8_t level)
{
    (void)on;
    (void)level;
    return -1;
}
int glsd_power_stage_apply_pb4_aux(uint8_t logical_level)
{
    (void)logical_level;
    return -1;
}
int glsd_power_stage_pc2_high(void) { return 0; }
int glsd_power_stage_pb4_high(void) { return 0; }

#endif
