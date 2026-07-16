#include "bsp_zdt_uart.h"

#include <stddef.h>
#include <string.h>

#include "cmsis_compiler.h"
#include "ti_msp_dl_config.h"

#if ZDT_GEN1_UART_TX_DMA_CHAN_ID != 1
#error "ZDT generation 1 UART TX must use physical DMA channel 1."
#endif

#if ZDT_GEN2_UART_TX_DMA_CHAN_ID != 2
#error "ZDT generation 2 UART TX must use physical DMA channel 2."
#endif

typedef struct {
    uint8_t tx_buffer[BSP_ZDT_UART_MAX_TX_BYTES];
    uint8_t rx_ring[BSP_ZDT_UART_RX_CAPACITY_BYTES];
    volatile uint16_t rx_head;
    volatile uint16_t rx_tail;
} bsp_zdt_uart_state_t;

static bsp_zdt_uart_state_t s_state[BSP_ZDT_UART_COUNT];
volatile bsp_zdt_uart_diagnostics_t
    g_bsp_zdt_uart_diag[BSP_ZDT_UART_COUNT];

static UART_Regs *BSP_ZdtUart_Instance(bsp_zdt_uart_port_t port)
{
    return (port == BSP_ZDT_UART_GEN1) ?
        ZDT_GEN1_UART_INST : ZDT_GEN2_UART_INST;
}

static uint8_t BSP_ZdtUart_DmaChannel(bsp_zdt_uart_port_t port)
{
    return (port == BSP_ZDT_UART_GEN1) ?
        ZDT_GEN1_UART_TX_DMA_CHAN_ID : ZDT_GEN2_UART_TX_DMA_CHAN_ID;
}

static uint16_t BSP_ZdtUart_UsedBytes(bsp_zdt_uart_port_t port)
{
    uint16_t head = s_state[port].rx_head;
    uint16_t tail = s_state[port].rx_tail;

    if (head >= tail) {
        return (uint16_t) (head - tail);
    }

    return (uint16_t) (BSP_ZDT_UART_RX_CAPACITY_BYTES -
        (uint16_t) (tail - head));
}

static void BSP_ZdtUart_ResetPort(bsp_zdt_uart_port_t port)
{
    uint8_t channel = BSP_ZdtUart_DmaChannel(port);
    UART_Regs *instance = BSP_ZdtUart_Instance(port);

    memset(&s_state[port], 0, sizeof(s_state[port]));
    memset((void *) &g_bsp_zdt_uart_diag[port], 0,
        sizeof(g_bsp_zdt_uart_diag[port]));
    g_bsp_zdt_uart_diag[port].tx_line_idle = 1U;
    g_bsp_zdt_uart_diag[port].initialized = 1U;

    DL_DMA_disableChannel(DMA, channel);
    DL_UART_Main_disableInterrupt(
        instance, DL_UART_MAIN_INTERRUPT_DMA_DONE_TX);
    DL_UART_Main_clearInterruptStatus(instance,
        DL_UART_MAIN_INTERRUPT_DMA_DONE_TX |
        DL_UART_MAIN_INTERRUPT_EOT_DONE |
        DL_UART_MAIN_INTERRUPT_RX);
}

void BSP_ZdtUart_Init(void)
{
    BSP_ZdtUart_ResetPort(BSP_ZDT_UART_GEN1);
    BSP_ZdtUart_ResetPort(BSP_ZDT_UART_GEN2);

    NVIC_ClearPendingIRQ(ZDT_GEN1_UART_INST_INT_IRQN);
    NVIC_SetPriority(ZDT_GEN1_UART_INST_INT_IRQN, 1U);
    NVIC_EnableIRQ(ZDT_GEN1_UART_INST_INT_IRQN);

    NVIC_ClearPendingIRQ(ZDT_GEN2_UART_INST_INT_IRQN);
    NVIC_SetPriority(ZDT_GEN2_UART_INST_INT_IRQN, 1U);
    NVIC_EnableIRQ(ZDT_GEN2_UART_INST_INT_IRQN);
}

bool BSP_ZdtUart_TryWrite(bsp_zdt_uart_port_t port,
    const uint8_t *data, uint8_t length)
{
    bsp_zdt_uart_state_t *state;
    volatile bsp_zdt_uart_diagnostics_t *diagnostics;
    UART_Regs *instance;
    uint8_t channel;

    if ((uint32_t) port >= (uint32_t) BSP_ZDT_UART_COUNT) {
        return false;
    }

    diagnostics = &g_bsp_zdt_uart_diag[port];
    if ((data == NULL) || (length == 0U) ||
        (length > BSP_ZDT_UART_MAX_TX_BYTES) ||
        (diagnostics->initialized == 0U)) {
        diagnostics->tx_argument_reject_count++;
        return false;
    }

    if ((diagnostics->tx_dma_busy != 0U) ||
        (diagnostics->tx_line_idle == 0U)) {
        diagnostics->tx_busy_reject_count++;
        return false;
    }

    state = &s_state[port];
    instance = BSP_ZdtUart_Instance(port);
    channel = BSP_ZdtUart_DmaChannel(port);
    memcpy(state->tx_buffer, data, length);

    DL_UART_Main_disableInterrupt(
        instance, DL_UART_MAIN_INTERRUPT_DMA_DONE_TX);
    DL_UART_Main_clearInterruptStatus(
        instance, DL_UART_MAIN_INTERRUPT_DMA_DONE_TX);
    DL_DMA_disableChannel(DMA, channel);
    DL_DMA_setSrcAddr(DMA, channel, (uint32_t) state->tx_buffer);
    DL_DMA_setDestAddr(
        DMA, channel, (uint32_t) &instance->TXDATA);
    DL_DMA_setTransferSize(DMA, channel, length);

    diagnostics->active_tx_length = length;
    diagnostics->tx_dma_busy = 1U;
    diagnostics->tx_line_idle = 0U;
    diagnostics->tx_frame_count++;
    diagnostics->tx_byte_count += length;
    __DMB();

    DL_UART_Main_clearInterruptStatus(
        instance, DL_UART_MAIN_INTERRUPT_DMA_DONE_TX);
    DL_UART_Main_enableInterrupt(
        instance, DL_UART_MAIN_INTERRUPT_DMA_DONE_TX);
    DL_DMA_enableChannel(DMA, channel);
    return true;
}

bool BSP_ZdtUart_TryRead(bsp_zdt_uart_port_t port, uint8_t *byte)
{
    uint16_t used;

    if (((uint32_t) port >= (uint32_t) BSP_ZDT_UART_COUNT) ||
        (byte == NULL) ||
        (s_state[port].rx_head == s_state[port].rx_tail)) {
        return false;
    }

    *byte = s_state[port].rx_ring[s_state[port].rx_tail];
    __DMB();
    s_state[port].rx_tail = (uint16_t) (
        (s_state[port].rx_tail + 1U) %
        BSP_ZDT_UART_RX_CAPACITY_BYTES);
    used = BSP_ZdtUart_UsedBytes(port);
    g_bsp_zdt_uart_diag[port].rx_used_bytes = used;
    return true;
}

uint16_t BSP_ZdtUart_Available(bsp_zdt_uart_port_t port)
{
    if ((uint32_t) port >= (uint32_t) BSP_ZDT_UART_COUNT) {
        return 0U;
    }

    return BSP_ZdtUart_UsedBytes(port);
}

void BSP_ZdtUart_FlushRx(bsp_zdt_uart_port_t port)
{
    if ((uint32_t) port >= (uint32_t) BSP_ZDT_UART_COUNT) {
        return;
    }

    s_state[port].rx_tail = s_state[port].rx_head;
    __DMB();
    g_bsp_zdt_uart_diag[port].rx_used_bytes = 0U;
    g_bsp_zdt_uart_diag[port].rx_flush_count++;
}

bool BSP_ZdtUart_IsTxIdle(bsp_zdt_uart_port_t port)
{
    if ((uint32_t) port >= (uint32_t) BSP_ZDT_UART_COUNT) {
        return false;
    }

    return (g_bsp_zdt_uart_diag[port].tx_dma_busy == 0U) &&
        (g_bsp_zdt_uart_diag[port].tx_line_idle != 0U);
}

static void BSP_ZdtUart_ReceiveBytes(bsp_zdt_uart_port_t port,
    UART_Regs *instance)
{
    while (!DL_UART_Main_isRXFIFOEmpty(instance)) {
        uint16_t next_head = (uint16_t) (
            (s_state[port].rx_head + 1U) %
            BSP_ZDT_UART_RX_CAPACITY_BYTES);
        uint8_t byte = DL_UART_Main_receiveData(instance);

        g_bsp_zdt_uart_diag[port].rx_byte_count++;
        if (next_head == s_state[port].rx_tail) {
            g_bsp_zdt_uart_diag[port].rx_overflow_count++;
            continue;
        }

        s_state[port].rx_ring[s_state[port].rx_head] = byte;
        __DMB();
        s_state[port].rx_head = next_head;
        g_bsp_zdt_uart_diag[port].rx_used_bytes =
            BSP_ZdtUart_UsedBytes(port);
        if (g_bsp_zdt_uart_diag[port].rx_used_bytes >
            g_bsp_zdt_uart_diag[port].rx_high_water_bytes) {
            g_bsp_zdt_uart_diag[port].rx_high_water_bytes =
                g_bsp_zdt_uart_diag[port].rx_used_bytes;
        }
    }
}

static void BSP_ZdtUart_IrqHandler(bsp_zdt_uart_port_t port,
    UART_Regs *instance)
{
    switch (DL_UART_Main_getPendingInterrupt(instance)) {
        case DL_UART_MAIN_IIDX_DMA_DONE_TX:
            if (g_bsp_zdt_uart_diag[port].tx_dma_busy == 0U) {
                g_bsp_zdt_uart_diag[port].unexpected_irq_count++;
                break;
            }
            g_bsp_zdt_uart_diag[port].active_tx_length = 0U;
            g_bsp_zdt_uart_diag[port].tx_dma_busy = 0U;
            g_bsp_zdt_uart_diag[port].tx_dma_done_count++;
            __DMB();
            break;

        case DL_UART_MAIN_IIDX_EOT_DONE:
            g_bsp_zdt_uart_diag[port].tx_line_idle = 1U;
            g_bsp_zdt_uart_diag[port].tx_eot_count++;
            __DMB();
            break;

        case DL_UART_MAIN_IIDX_RX:
            BSP_ZdtUart_ReceiveBytes(port, instance);
            break;

        default:
            g_bsp_zdt_uart_diag[port].unexpected_irq_count++;
            break;
    }
}

void ZDT_GEN1_UART_INST_IRQHandler(void)
{
    BSP_ZdtUart_IrqHandler(BSP_ZDT_UART_GEN1, ZDT_GEN1_UART_INST);
}

void ZDT_GEN2_UART_INST_IRQHandler(void)
{
    BSP_ZdtUart_IrqHandler(BSP_ZDT_UART_GEN2, ZDT_GEN2_UART_INST);
}
