#ifndef ECHO_BSP_TFMINI_UART_H
#define ECHO_BSP_TFMINI_UART_H

#include <stdbool.h>
#include <stdint.h>

#define BSP_TFMINI_UART_RX_CAPACITY_BYTES 128U
#define BSP_TFMINI_UART_TX_CAPACITY_BYTES 16U

typedef struct {
    uint32_t rx_byte_count;
    uint32_t rx_overflow_count;
    uint32_t tx_request_count;
    uint32_t tx_complete_count;
    uint32_t tx_byte_count;
    uint32_t tx_rejected_busy_count;
    uint32_t tx_rejected_argument_count;
    uint32_t irq_entry_count;
    uint32_t irq_exit_count;
    uint32_t irq_drain_guard_count;
    uint32_t unexpected_iidx_count;
    uint32_t rx_recovery_count;
    uint32_t rx_recovery_discarded_bytes;
    uint16_t rx_high_water_bytes;
    uint8_t tx_busy;
    uint8_t initialized;
    uint32_t last_iidx;
} bsp_tfmini_uart_diagnostics_t;

extern volatile bsp_tfmini_uart_diagnostics_t g_bsp_tfmini_uart_diag;

void BSP_TfminiUart_Init(void);
void BSP_TfminiUart_RecoverRx(void);
bool BSP_TfminiUart_TryRead(uint8_t *byte);
bool BSP_TfminiUart_TryWrite(const uint8_t *data, uint8_t length);
void BSP_TfminiUart_ServiceTx(void);
const volatile bsp_tfmini_uart_diagnostics_t *
    BSP_TfminiUart_GetDiagnostics(void);

#endif
