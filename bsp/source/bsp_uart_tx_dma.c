#include "bsp_uart_tx_dma.h"

#include <stddef.h>

#include "ti_msp_dl_config.h"

#if DEBUG_UART_TX_DMA_CHAN_ID != 3
#error "DEBUG UART TX must use physical DMA channel 3."
#endif

static bsp_uart_tx_dma_callback_t s_complete_callback;
static bsp_uart_rx_byte_callback_t s_rx_byte_callback;
volatile bsp_uart_tx_dma_diagnostics_t g_bsp_uart_tx_dma_diag;

#define s_diagnostics g_bsp_uart_tx_dma_diag

void BSP_UartTxDma_Init(bsp_uart_tx_dma_callback_t complete_callback)
{
    s_complete_callback = complete_callback;
    s_diagnostics.start_count = 0U;
    s_diagnostics.dma_done_count = 0U;
    s_diagnostics.unexpected_dma_done_count = 0U;
    s_diagnostics.eot_count = 0U;
    s_diagnostics.rejected_busy_count = 0U;
    s_diagnostics.rejected_argument_count = 0U;
    s_diagnostics.stale_channel_disable_count = 0U;
    s_diagnostics.abort_count = 0U;
    s_diagnostics.rx_byte_count = 0U;
    s_diagnostics.rx_dropped_no_callback_count = 0U;
    s_diagnostics.active_length = 0U;
    s_diagnostics.dma_busy = 0U;
    s_diagnostics.line_idle = 1U;
    s_diagnostics.initialized = 1U;
    s_diagnostics.dma_done_pending = 0U;

    DL_DMA_disableChannel(DMA, DEBUG_UART_TX_DMA_CHAN_ID);
    DL_UART_Main_clearInterruptStatus(DEBUG_UART_INST,
        DL_UART_MAIN_INTERRUPT_DMA_DONE_TX |
            DL_UART_MAIN_INTERRUPT_EOT_DONE |
            DL_UART_MAIN_INTERRUPT_RX);
    NVIC_ClearPendingIRQ(DEBUG_UART_INST_INT_IRQN);
    NVIC_SetPriority(DEBUG_UART_INST_INT_IRQN, 0U);
    NVIC_EnableIRQ(DEBUG_UART_INST_INT_IRQN);
}

void BSP_UartRx_SetCallback(bsp_uart_rx_byte_callback_t byte_callback)
{
    s_rx_byte_callback = byte_callback;
}

bool BSP_UartTxDma_Start(const uint8_t *data, uint16_t length)
{
    if ((data == NULL) || (length == 0U) ||
        (s_complete_callback == NULL) ||
        (s_diagnostics.initialized == 0U)) {
        s_diagnostics.rejected_argument_count++;
        return false;
    }

    if (s_diagnostics.dma_busy != 0U) {
        s_diagnostics.rejected_busy_count++;
        return false;
    }

    DL_UART_Main_disableInterrupt(DEBUG_UART_INST,
        DL_UART_MAIN_INTERRUPT_DMA_DONE_TX |
            DL_UART_MAIN_INTERRUPT_EOT_DONE);
    DL_UART_Main_clearInterruptStatus(
        DEBUG_UART_INST, DL_UART_MAIN_INTERRUPT_DMA_DONE_TX |
            DL_UART_MAIN_INTERRUPT_EOT_DONE);
    if (DL_DMA_isChannelEnabled(DMA, DEBUG_UART_TX_DMA_CHAN_ID)) {
        DL_DMA_disableChannel(DMA, DEBUG_UART_TX_DMA_CHAN_ID);
        s_diagnostics.stale_channel_disable_count++;
    }

    DL_DMA_setSrcAddr(DMA, DEBUG_UART_TX_DMA_CHAN_ID, (uint32_t) data);
    DL_DMA_setDestAddr(
        DMA, DEBUG_UART_TX_DMA_CHAN_ID,
        (uint32_t) &DEBUG_UART_INST->TXDATA);
    DL_DMA_setTransferSize(DMA, DEBUG_UART_TX_DMA_CHAN_ID, length);

    s_diagnostics.active_length = length;
    s_diagnostics.dma_busy = 1U;
    s_diagnostics.line_idle = 0U;
    s_diagnostics.dma_done_pending = 0U;
    s_diagnostics.start_count++;
    __DMB();
    DL_UART_Main_clearInterruptStatus(DEBUG_UART_INST,
        DL_UART_MAIN_INTERRUPT_DMA_DONE_TX |
            DL_UART_MAIN_INTERRUPT_EOT_DONE);
    DL_UART_Main_enableInterrupt(DEBUG_UART_INST,
        DL_UART_MAIN_INTERRUPT_DMA_DONE_TX |
            DL_UART_MAIN_INTERRUPT_EOT_DONE);
    DL_DMA_enableChannel(DMA, DEBUG_UART_TX_DMA_CHAN_ID);
    return true;
}

void BSP_UartTxDma_Abort(void)
{
    DL_UART_Main_disableInterrupt(
        DEBUG_UART_INST, DL_UART_MAIN_INTERRUPT_DMA_DONE_TX);
    DL_DMA_disableChannel(DMA, DEBUG_UART_TX_DMA_CHAN_ID);
    DL_UART_Main_clearInterruptStatus(
        DEBUG_UART_INST, DL_UART_MAIN_INTERRUPT_DMA_DONE_TX);
    s_diagnostics.active_length = 0U;
    s_diagnostics.dma_busy = 0U;
    s_diagnostics.dma_done_pending = 0U;
    s_diagnostics.line_idle =
        DL_UART_Main_isBusy(DEBUG_UART_INST) ? 0U : 1U;
    s_diagnostics.abort_count++;
    __DMB();
}

bool BSP_UartTxDma_IsDmaBusy(void)
{
    return (s_diagnostics.dma_busy != 0U);
}

bool BSP_UartTxDma_IsLineIdle(void)
{
    return (s_diagnostics.line_idle != 0U);
}

const volatile bsp_uart_tx_dma_diagnostics_t *
    BSP_UartTxDma_GetDiagnostics(void)
{
    return &s_diagnostics;
}

void DEBUG_UART_INST_IRQHandler(void)
{
    switch (DL_UART_Main_getPendingInterrupt(DEBUG_UART_INST)) {
        case DL_UART_MAIN_IIDX_DMA_DONE_TX:
            if (s_diagnostics.dma_busy == 0U) {
                s_diagnostics.unexpected_dma_done_count++;
                break;
            }

            s_diagnostics.dma_busy = 0U;
            s_diagnostics.dma_done_pending = 1U;
            s_diagnostics.dma_done_count++;
            __DMB();
            break;

        case DL_UART_MAIN_IIDX_EOT_DONE:
            s_diagnostics.line_idle = 1U;
            s_diagnostics.eot_count++;
            if (s_diagnostics.dma_done_pending != 0U) {
                s_diagnostics.dma_done_pending = 0U;
                s_diagnostics.active_length = 0U;
                __DMB();
                if (s_complete_callback != NULL) {
                    s_complete_callback();
                }
            }
            break;

        case DL_UART_MAIN_IIDX_RX:
            while (!DL_UART_Main_isRXFIFOEmpty(DEBUG_UART_INST)) {
                uint8_t byte =
                    DL_UART_Main_receiveData(DEBUG_UART_INST);

                s_diagnostics.rx_byte_count++;
                if (s_rx_byte_callback != NULL) {
                    s_rx_byte_callback(byte);
                } else {
                    s_diagnostics.rx_dropped_no_callback_count++;
                }
            }
            break;

        default:
            break;
    }
}
