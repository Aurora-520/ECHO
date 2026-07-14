#include "serial_tx.h"

#include <stddef.h>

#include "FreeRTOS.h"
#include "bsp_time.h"
#include "bsp_uart_tx_dma.h"
#include "task.h"

#define SERIAL_TX_STALL_TIMEOUT_US 250000UL

static uint8_t s_ring[SERIAL_TX_RING_CAPACITY_BYTES];
static volatile uint16_t s_head;
static volatile uint16_t s_tail;
static volatile uint16_t s_dma_length;
volatile serial_tx_diagnostics_t g_serial_tx_diag;

#define s_diagnostics g_serial_tx_diag

static uint16_t SerialTx_UsedBytes(void)
{
    uint16_t head = s_head;
    uint16_t tail = s_tail;

    if (head >= tail) {
        return (uint16_t) (head - tail);
    }

    return (uint16_t) (
        SERIAL_TX_RING_CAPACITY_BYTES - (uint16_t) (tail - head));
}

static void SerialTx_StartNextBlock(void)
{
    uint16_t length;

    if ((s_diagnostics.quiet_window_active != 0U) ||
        (s_diagnostics.dma_active != 0U) || (s_head == s_tail)) {
        return;
    }

    if (s_head > s_tail) {
        length = (uint16_t) (s_head - s_tail);
    } else {
        length =
            (uint16_t) (SERIAL_TX_RING_CAPACITY_BYTES - s_tail);
    }

    s_dma_length = length;
    s_diagnostics.active_dma_length = length;
    s_diagnostics.dma_active = 1U;
    s_diagnostics.last_dma_start_us = BSP_Time_GetUs();

    if (!BSP_UartTxDma_Start(&s_ring[s_tail], length)) {
        s_dma_length = 0U;
        s_diagnostics.active_dma_length = 0U;
        s_diagnostics.dma_active = 0U;
        s_diagnostics.dma_start_fail_count++;
    }
}

static void SerialTx_OnDmaComplete(void)
{
    if ((s_diagnostics.dma_active == 0U) || (s_dma_length == 0U)) {
        s_diagnostics.unexpected_dma_done_count++;
        return;
    }

    s_tail = (uint16_t) ((s_tail + s_dma_length) %
                         SERIAL_TX_RING_CAPACITY_BYTES);
    s_diagnostics.dma_block_count++;
    s_diagnostics.dma_bytes_completed += s_dma_length;
    s_diagnostics.last_dma_done_us = BSP_Time_GetUs();
    s_diagnostics.ring_used_bytes = SerialTx_UsedBytes();
    s_diagnostics.active_dma_length = 0U;
    s_diagnostics.dma_active = 0U;
    s_dma_length = 0U;

    SerialTx_StartNextBlock();
}

void SerialTx_Init(void)
{
    s_head = 0U;
    s_tail = 0U;
    s_dma_length = 0U;
    s_diagnostics.write_accepted_count = 0U;
    s_diagnostics.write_dropped_count = 0U;
    s_diagnostics.bytes_accepted = 0U;
    s_diagnostics.dma_block_count = 0U;
    s_diagnostics.dma_bytes_completed = 0U;
    s_diagnostics.dma_start_fail_count = 0U;
    s_diagnostics.unexpected_dma_done_count = 0U;
    s_diagnostics.dma_stall_count = 0U;
    s_diagnostics.dma_restart_count = 0U;
    s_diagnostics.ring_high_water_bytes = 0U;
    s_diagnostics.last_dma_start_us = 0U;
    s_diagnostics.last_dma_done_us = 0U;
    s_diagnostics.max_write_critical_us = 0U;
    s_diagnostics.quiet_window_attempt_count = 0U;
    s_diagnostics.quiet_window_acquired_count = 0U;
    s_diagnostics.quiet_window_rejected_count = 0U;
    s_diagnostics.quiet_window_release_count = 0U;
    s_diagnostics.quiet_window_start_us = 0U;
    s_diagnostics.max_quiet_window_us = 0U;
    s_diagnostics.ring_used_bytes = 0U;
    s_diagnostics.active_dma_length = 0U;
    s_diagnostics.dma_active = 0U;
    s_diagnostics.quiet_window_active = 0U;
    s_diagnostics.initialized = 1U;
    s_diagnostics.reserved = 0U;

    BSP_UartTxDma_Init(SerialTx_OnDmaComplete);
}

bool SerialTx_TryBeginQuietWindow(void)
{
    bool acquired = false;

    taskENTER_CRITICAL();
    s_diagnostics.quiet_window_attempt_count++;
    if ((s_diagnostics.initialized != 0U) &&
        (s_diagnostics.quiet_window_active == 0U) &&
        (s_diagnostics.dma_active == 0U) &&
        (s_head == s_tail) &&
        BSP_UartTxDma_IsLineIdle()) {
        s_diagnostics.quiet_window_active = 1U;
        s_diagnostics.quiet_window_acquired_count++;
        s_diagnostics.quiet_window_start_us = BSP_Time_GetUs();
        acquired = true;
    } else {
        s_diagnostics.quiet_window_rejected_count++;
    }
    taskEXIT_CRITICAL();
    return acquired;
}

void SerialTx_EndQuietWindow(void)
{
    taskENTER_CRITICAL();
    if (s_diagnostics.quiet_window_active != 0U) {
        uint32_t duration_us = (uint32_t) (
            BSP_Time_GetUs() - s_diagnostics.quiet_window_start_us);

        if (duration_us > s_diagnostics.max_quiet_window_us) {
            s_diagnostics.max_quiet_window_us = duration_us;
        }
        s_diagnostics.quiet_window_start_us = 0U;
        s_diagnostics.quiet_window_active = 0U;
        s_diagnostics.quiet_window_release_count++;
        SerialTx_StartNextBlock();
    }
    taskEXIT_CRITICAL();
}

void SerialTx_Service(void)
{
    uint32_t now_us = BSP_Time_GetUs();

    taskENTER_CRITICAL();
    if ((s_diagnostics.dma_active != 0U) &&
        ((uint32_t) (now_us - s_diagnostics.last_dma_start_us) >=
            SERIAL_TX_STALL_TIMEOUT_US)) {
        BSP_UartTxDma_Abort();
        s_dma_length = 0U;
        s_diagnostics.active_dma_length = 0U;
        s_diagnostics.dma_active = 0U;
        s_diagnostics.dma_stall_count++;
        s_diagnostics.dma_restart_count++;
    }

    SerialTx_StartNextBlock();
    taskEXIT_CRITICAL();
}

static void SerialTx_RecordCriticalDuration(uint32_t start_us)
{
    uint32_t duration_us =
        (uint32_t) (BSP_Time_GetUs() - start_us);

    if (duration_us > s_diagnostics.max_write_critical_us) {
        s_diagnostics.max_write_critical_us = duration_us;
    }
}

bool SerialTx_TryWrite(const uint8_t *data, uint16_t length)
{
    uint16_t index;
    uint16_t used;
    uint16_t free_bytes;
    uint32_t critical_start_us;

    if ((data == NULL) || (length == 0U) ||
        (length > SERIAL_TX_MAX_WRITE_BYTES) ||
        (s_diagnostics.initialized == 0U)) {
        s_diagnostics.write_dropped_count++;
        return false;
    }

    critical_start_us = BSP_Time_GetUs();
    taskENTER_CRITICAL();
    used = SerialTx_UsedBytes();
    free_bytes =
        (uint16_t) ((SERIAL_TX_RING_CAPACITY_BYTES - 1U) - used);

    if (length > free_bytes) {
        s_diagnostics.write_dropped_count++;
        taskEXIT_CRITICAL();
        SerialTx_RecordCriticalDuration(critical_start_us);
        return false;
    }

    for (index = 0U; index < length; index++) {
        s_ring[s_head] = data[index];
        s_head = (uint16_t) ((s_head + 1U) %
                             SERIAL_TX_RING_CAPACITY_BYTES);
    }

    used = (uint16_t) (used + length);
    s_diagnostics.write_accepted_count++;
    s_diagnostics.bytes_accepted += length;
    s_diagnostics.ring_used_bytes = used;
    if (used > s_diagnostics.ring_high_water_bytes) {
        s_diagnostics.ring_high_water_bytes = used;
    }

    SerialTx_StartNextBlock();
    taskEXIT_CRITICAL();
    SerialTx_RecordCriticalDuration(critical_start_us);
    return true;
}

const volatile serial_tx_diagnostics_t *SerialTx_GetDiagnostics(void)
{
    return &s_diagnostics;
}
