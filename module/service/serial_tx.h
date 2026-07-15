#ifndef ECHO_SERIAL_TX_H
#define ECHO_SERIAL_TX_H

#include <stdbool.h>
#include <stdint.h>

#define SERIAL_TX_RING_CAPACITY_BYTES 1024U
#define SERIAL_TX_MAX_WRITE_BYTES     128U

typedef struct {
    uint32_t write_accepted_count;
    uint32_t write_dropped_count;
    uint32_t bytes_accepted;
    uint32_t dma_block_count;
    uint32_t dma_bytes_completed;
    uint32_t dma_start_fail_count;
    uint32_t unexpected_dma_done_count;
    uint32_t dma_stall_count;
    uint32_t dma_restart_count;
    uint32_t ring_high_water_bytes;
    uint32_t last_dma_start_us;
    uint32_t last_dma_done_us;
    uint32_t max_write_critical_us;
    uint32_t quiet_window_attempt_count;
    uint32_t quiet_window_acquired_count;
    uint32_t quiet_window_rejected_count;
    uint32_t quiet_window_release_count;
    uint32_t quiet_window_start_us;
    uint32_t max_quiet_window_us;
    uint16_t ring_used_bytes;
    uint16_t active_dma_length;
    uint8_t dma_active;
    uint8_t quiet_window_active;
    uint8_t initialized;
    uint8_t reserved;
} serial_tx_diagnostics_t;

extern volatile serial_tx_diagnostics_t g_serial_tx_diag;

void SerialTx_Init(void);
bool SerialTx_TryBeginQuietWindow(void);
bool SerialTx_TryBeginPriorityQuietWindow(void);
void SerialTx_EndQuietWindow(void);

/*
 * Queues one complete frame without waiting. The function either accepts all
 * bytes or drops the complete frame.
 */
bool SerialTx_TryWrite(const uint8_t *data, uint16_t length);
void SerialTx_Service(void);

const volatile serial_tx_diagnostics_t *SerialTx_GetDiagnostics(void);

#endif
