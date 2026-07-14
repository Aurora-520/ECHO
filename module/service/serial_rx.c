#include "serial_rx.h"

#include <stddef.h>

#include "bsp_uart_tx_dma.h"
#include "cmsis_compiler.h"
#include "FreeRTOS.h"
#include "task.h"

static uint8_t s_ring[SERIAL_RX_RING_CAPACITY_BYTES];
static volatile uint16_t s_head;
static volatile uint16_t s_tail;
volatile serial_rx_diagnostics_t g_serial_rx_diag;

static uint16_t SerialRx_UsedBytes(void)
{
    uint16_t head = s_head;
    uint16_t tail = s_tail;

    if (head >= tail) {
        return (uint16_t) (head - tail);
    }

    return (uint16_t) (
        SERIAL_RX_RING_CAPACITY_BYTES - (uint16_t) (tail - head));
}

static void SerialRx_OnByte(uint8_t byte)
{
    uint16_t used;
    uint16_t next_head = (uint16_t) (
        (s_head + 1U) % SERIAL_RX_RING_CAPACITY_BYTES);

    if (next_head == s_tail) {
        g_serial_rx_diag.overflow_count++;
        return;
    }

    s_ring[s_head] = byte;
    __DMB();
    s_head = next_head;
    g_serial_rx_diag.received_byte_count++;
    used = SerialRx_UsedBytes();
    g_serial_rx_diag.used_bytes = used;
    if (used > g_serial_rx_diag.high_water_bytes) {
        g_serial_rx_diag.high_water_bytes = used;
    }
}

void SerialRx_Init(void)
{
    s_head = 0U;
    s_tail = 0U;
    g_serial_rx_diag.received_byte_count = 0U;
    g_serial_rx_diag.overflow_count = 0U;
    g_serial_rx_diag.flush_count = 0U;
    g_serial_rx_diag.used_bytes = 0U;
    g_serial_rx_diag.high_water_bytes = 0U;
    g_serial_rx_diag.initialized = 1U;
    BSP_UartRx_SetCallback(SerialRx_OnByte);
}

bool SerialRx_TryRead(uint8_t *byte)
{
    uint16_t used;

    if ((byte == NULL) || (s_head == s_tail)) {
        return false;
    }

    *byte = s_ring[s_tail];
    __DMB();
    s_tail = (uint16_t) (
        (s_tail + 1U) % SERIAL_RX_RING_CAPACITY_BYTES);
    used = SerialRx_UsedBytes();
    g_serial_rx_diag.used_bytes = used;
    return true;
}

uint16_t SerialRx_Available(void)
{
    return SerialRx_UsedBytes();
}

uint32_t SerialRx_GetOverflowCount(void)
{
    return g_serial_rx_diag.overflow_count;
}

void SerialRx_Flush(void)
{
    taskENTER_CRITICAL();
    s_tail = s_head;
    __DMB();
    g_serial_rx_diag.used_bytes = 0U;
    g_serial_rx_diag.flush_count++;
    taskEXIT_CRITICAL();
}
