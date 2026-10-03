#include "hw_stub.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "host_types.h"

#include "glsd301p_hw_io.h"
#include "glsd301p_uart_frame.h"

static uint32_t g_tick;
static uint16_t g_except_line;
static uint8_t g_except_code;
static uint32_t g_except_count;

static bool g_uart_busy;
static bool g_uart_accepts = true;
static uint32_t g_uart_attempts;
static uint8_t g_uart_last[GLSD301P_CONTROL_FRAME_SIZE];
static bool g_uart_last_valid;

static bool g_pc2_high;
static bool g_pb4_high;

void host_clock_set(uint32_t tick)
{
    g_tick = tick;
}

void host_clock_advance(uint32_t delta_ticks)
{
    g_tick += delta_ticks;
}

uint32_t host_clock_get(void)
{
    return g_tick;
}

uint16_t host_exception_line(void)
{
    return g_except_line;
}

uint8_t host_exception_code(void)
{
    return g_except_code;
}

uint32_t host_exception_count(void)
{
    return g_except_count;
}

void host_exception_reset(void)
{
    g_except_line = 0u;
    g_except_code = 0u;
    g_except_count = 0u;
}

void host_uart_set_busy(bool busy)
{
    g_uart_busy = busy;
}

void host_uart_set_send_accepts(bool accepts)
{
    g_uart_accepts = accepts;
}

uint32_t host_uart_send_attempts(void)
{
    return g_uart_attempts;
}

bool host_uart_last_frame(uint8_t out[6])
{
    if (out == NULL || !g_uart_last_valid) {
        return false;
    }
    memcpy(out, g_uart_last, sizeof(g_uart_last));
    return true;
}

void host_gpio_set(bool pc2_high, bool pb4_high)
{
    g_pc2_high = pc2_high;
    g_pb4_high = pb4_high;
}

void host_stub_reset(void)
{
    host_exception_reset();
    g_uart_busy = false;
    g_uart_accepts = true;
    g_uart_attempts = 0u;
    g_uart_last_valid = false;
    memset(g_uart_last, 0, sizeof(g_uart_last));
    g_pc2_high = false;
    g_pb4_high = false;
}

/* --- SDK seams ------------------------------------------------------ */

u32 clock_time(void)
{
    return g_tick;
}

u32 drv_disable_irq(void)
{
    return 0u;
}

void drv_restore_irq(u32 level)
{
    (void)level;
}

volatile u16 T_evtExcept[4];

u8 sys_exceptionPost(u16 line, u8 evt)
{
    T_evtExcept[0] = line;
    T_evtExcept[1] = evt;
    g_except_line = line;
    g_except_code = evt;
    g_except_count++;
    return 0;
}

/* --- glsd301p_hw_io seam -------------------------------------------- */

bool glsd301p_hw_uart_busy(void)
{
    return g_uart_busy;
}

bool glsd301p_hw_uart_send_frame(
    const uint8_t frame[GLSD301P_CONTROL_FRAME_SIZE])
{
    g_uart_attempts++;
    if (frame == NULL) {
        return false;
    }
    memcpy(g_uart_last, frame, sizeof(g_uart_last));
    g_uart_last_valid = true;
    return g_uart_accepts;
}

bool glsd301p_hw_gpio_pc2_high(void)
{
    return g_pc2_high;
}

bool glsd301p_hw_gpio_pb4_high(void)
{
    return g_pb4_high;
}
