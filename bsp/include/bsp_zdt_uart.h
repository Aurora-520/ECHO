#ifndef ECHO_BSP_ZDT_UART_H
#define ECHO_BSP_ZDT_UART_H

#include <stdbool.h>
#include <stdint.h>

#define BSP_ZDT_UART_RX_CAPACITY_BYTES 64U
#define BSP_ZDT_UART_MAX_TX_BYTES      32U

typedef enum {
    BSP_ZDT_UART_GEN1 = 0U,
    BSP_ZDT_UART_GEN2,
    BSP_ZDT_UART_COUNT
} bsp_zdt_uart_port_t;

typedef struct {
    uint32_t tx_frame_count;
    uint32_t tx_byte_count;
    uint32_t tx_busy_reject_count;
    uint32_t tx_argument_reject_count;
    uint32_t tx_dma_done_count;
    uint32_t tx_eot_count;
    uint32_t rx_byte_count;
    uint32_t rx_overflow_count;
    uint32_t rx_flush_count;
    uint32_t unexpected_irq_count;
    uint16_t rx_used_bytes;
    uint16_t rx_high_water_bytes;
    uint8_t active_tx_length;
    uint8_t tx_dma_busy;
    uint8_t tx_line_idle;
    uint8_t initialized;
} bsp_zdt_uart_diagnostics_t;

extern volatile bsp_zdt_uart_diagnostics_t
    g_bsp_zdt_uart_diag[BSP_ZDT_UART_COUNT];

void BSP_ZdtUart_Init(void);
bool BSP_ZdtUart_TryWrite(bsp_zdt_uart_port_t port,
    const uint8_t *data, uint8_t length);
bool BSP_ZdtUart_TryRead(bsp_zdt_uart_port_t port, uint8_t *byte);
uint16_t BSP_ZdtUart_Available(bsp_zdt_uart_port_t port);
void BSP_ZdtUart_FlushRx(bsp_zdt_uart_port_t port);
bool BSP_ZdtUart_IsTxIdle(bsp_zdt_uart_port_t port);

#endif
