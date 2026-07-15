#ifndef ECHO_BSP_UART_TX_DMA_H
#define ECHO_BSP_UART_TX_DMA_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*bsp_uart_tx_dma_callback_t)(void);
typedef void (*bsp_uart_rx_byte_callback_t)(uint8_t byte);

typedef struct {
    uint32_t start_count;
    uint32_t dma_done_count;
    uint32_t unexpected_dma_done_count;
    uint32_t eot_count;
    uint32_t rejected_busy_count;
    uint32_t rejected_argument_count;
    uint32_t stale_channel_disable_count;
    uint32_t abort_count;
    uint32_t rx_byte_count;
    uint32_t rx_dropped_no_callback_count;
    uint16_t active_length;
    uint8_t dma_busy;
    uint8_t line_idle;
    uint8_t initialized;
    uint8_t dma_done_pending;
} bsp_uart_tx_dma_diagnostics_t;

extern volatile bsp_uart_tx_dma_diagnostics_t
    g_bsp_uart_tx_dma_diag;

void BSP_UartTxDma_Init(bsp_uart_tx_dma_callback_t complete_callback);
void BSP_UartRx_SetCallback(bsp_uart_rx_byte_callback_t byte_callback);
bool BSP_UartTxDma_Start(const uint8_t *data, uint16_t length);
void BSP_UartTxDma_Abort(void);
bool BSP_UartTxDma_IsDmaBusy(void);
bool BSP_UartTxDma_IsLineIdle(void);
const volatile bsp_uart_tx_dma_diagnostics_t *
    BSP_UartTxDma_GetDiagnostics(void);

#endif
