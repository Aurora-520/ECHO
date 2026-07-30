#include "bsp_tfmini_uart.h"

#include <stddef.h>
#include <string.h>

#include "cmsis_compiler.h"
#include "ti_msp_dl_config.h"

#if LIDAR_UART_BAUD_RATE != 115200
#error "TFmini-S UART must start at its default 115200 baud."
#endif

#define BSP_TFMINI_UART_ISR_MAX_DRAIN_BYTES 32U

static uint8_t s_rx_ring[BSP_TFMINI_UART_RX_CAPACITY_BYTES];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;
static uint8_t s_tx_buffer[BSP_TFMINI_UART_TX_CAPACITY_BYTES];
static uint8_t s_tx_length;
static uint8_t s_tx_offset;

volatile bsp_tfmini_uart_diagnostics_t g_bsp_tfmini_uart_diag;

static uint16_t BSP_TfminiUart_RxUsed(void)
{
    uint16_t head = s_rx_head;
    uint16_t tail = s_rx_tail;

    if (head >= tail) {
        return (uint16_t) (head - tail);
    }
    return (uint16_t) (BSP_TFMINI_UART_RX_CAPACITY_BYTES -
        (uint16_t) (tail - head));
}

static void BSP_TfminiUart_OnRxByte(uint8_t byte)
{
    uint16_t used;
    uint16_t next_head = (uint16_t) ((s_rx_head + 1U) %
        BSP_TFMINI_UART_RX_CAPACITY_BYTES);

    if (next_head == s_rx_tail) {
        g_bsp_tfmini_uart_diag.rx_overflow_count++;
        return;
    }

    s_rx_ring[s_rx_head] = byte;
    __DMB();
    s_rx_head = next_head;
    g_bsp_tfmini_uart_diag.rx_byte_count++;
    used = BSP_TfminiUart_RxUsed();
    if (used > g_bsp_tfmini_uart_diag.rx_high_water_bytes) {
        g_bsp_tfmini_uart_diag.rx_high_water_bytes = used;
    }
}

void BSP_TfminiUart_Init(void)
{
    s_rx_head = 0U;
    s_rx_tail = 0U;
    s_tx_length = 0U;
    s_tx_offset = 0U;
    memset((void *) &g_bsp_tfmini_uart_diag, 0,
        sizeof(g_bsp_tfmini_uart_diag));
    g_bsp_tfmini_uart_diag.initialized = 1U;

    DL_UART_Main_clearInterruptStatus(
        LIDAR_UART_INST, DL_UART_MAIN_INTERRUPT_RX);
    DL_UART_Main_enableInterrupt(
        LIDAR_UART_INST, DL_UART_MAIN_INTERRUPT_RX);
    NVIC_ClearPendingIRQ(LIDAR_UART_INST_INT_IRQN);
    NVIC_SetPriority(LIDAR_UART_INST_INT_IRQN, 1U);
    NVIC_EnableIRQ(LIDAR_UART_INST_INT_IRQN);
}

void BSP_TfminiUart_RecoverRx(void)
{
    uint32_t discarded = 0U;

    NVIC_DisableIRQ(LIDAR_UART_INST_INT_IRQN);
    DL_UART_Main_disableInterrupt(
        LIDAR_UART_INST, DL_UART_MAIN_INTERRUPT_RX);
    DL_UART_Main_reset(LIDAR_UART_INST);
    DL_UART_Main_enablePower(LIDAR_UART_INST);
    delay_cycles(POWER_STARTUP_DELAY);
    s_rx_head = 0U;
    s_rx_tail = 0U;
    SYSCFG_DL_LIDAR_UART_init();
    while (!DL_UART_Main_isRXFIFOEmpty(LIDAR_UART_INST)) {
        (void) DL_UART_Main_receiveData(LIDAR_UART_INST);
        discarded++;
    }
    DL_UART_Main_clearInterruptStatus(
        LIDAR_UART_INST, DL_UART_MAIN_INTERRUPT_RX);
    NVIC_ClearPendingIRQ(LIDAR_UART_INST_INT_IRQN);
    NVIC_SetPriority(LIDAR_UART_INST_INT_IRQN, 1U);
    NVIC_EnableIRQ(LIDAR_UART_INST_INT_IRQN);
    g_bsp_tfmini_uart_diag.rx_recovery_count++;
    g_bsp_tfmini_uart_diag.rx_recovery_discarded_bytes += discarded;
}

bool BSP_TfminiUart_TryRead(uint8_t *byte)
{
    if ((byte == NULL) || (s_rx_head == s_rx_tail)) {
        return false;
    }

    *byte = s_rx_ring[s_rx_tail];
    __DMB();
    s_rx_tail = (uint16_t) ((s_rx_tail + 1U) %
        BSP_TFMINI_UART_RX_CAPACITY_BYTES);
    return true;
}

bool BSP_TfminiUart_TryWrite(const uint8_t *data, uint8_t length)
{
    if ((data == NULL) || (length == 0U) ||
        (length > BSP_TFMINI_UART_TX_CAPACITY_BYTES) ||
        (g_bsp_tfmini_uart_diag.initialized == 0U)) {
        g_bsp_tfmini_uart_diag.tx_rejected_argument_count++;
        return false;
    }
    if (g_bsp_tfmini_uart_diag.tx_busy != 0U) {
        g_bsp_tfmini_uart_diag.tx_rejected_busy_count++;
        return false;
    }

    memcpy(s_tx_buffer, data, length);
    s_tx_length = length;
    s_tx_offset = 0U;
    g_bsp_tfmini_uart_diag.tx_busy = 1U;
    g_bsp_tfmini_uart_diag.tx_request_count++;
    __DMB();
    BSP_TfminiUart_ServiceTx();
    return true;
}

void BSP_TfminiUart_ServiceTx(void)
{
    if (g_bsp_tfmini_uart_diag.tx_busy == 0U) {
        return;
    }

    while ((s_tx_offset < s_tx_length) &&
        !DL_UART_Main_isTXFIFOFull(LIDAR_UART_INST)) {
        DL_UART_Main_transmitData(
            LIDAR_UART_INST, s_tx_buffer[s_tx_offset]);
        s_tx_offset++;
        g_bsp_tfmini_uart_diag.tx_byte_count++;
    }

    if (s_tx_offset >= s_tx_length) {
        s_tx_length = 0U;
        s_tx_offset = 0U;
        g_bsp_tfmini_uart_diag.tx_busy = 0U;
        g_bsp_tfmini_uart_diag.tx_complete_count++;
        __DMB();
    }
}

const volatile bsp_tfmini_uart_diagnostics_t *
    BSP_TfminiUart_GetDiagnostics(void)
{
    return &g_bsp_tfmini_uart_diag;
}

void LIDAR_UART_INST_IRQHandler(void)
{
    DL_UART_IIDX iidx =
        DL_UART_Main_getPendingInterrupt(LIDAR_UART_INST);

    g_bsp_tfmini_uart_diag.irq_entry_count++;
    g_bsp_tfmini_uart_diag.last_iidx = (uint32_t) iidx;
    switch (iidx) {
        case DL_UART_MAIN_IIDX_RX:
        {
            uint8_t drained = 0U;

            while (!DL_UART_Main_isRXFIFOEmpty(LIDAR_UART_INST) &&
                (drained < BSP_TFMINI_UART_ISR_MAX_DRAIN_BYTES)) {
                BSP_TfminiUart_OnRxByte(
                    DL_UART_Main_receiveData(LIDAR_UART_INST));
                drained++;
            }
            if (!DL_UART_Main_isRXFIFOEmpty(LIDAR_UART_INST)) {
                g_bsp_tfmini_uart_diag.irq_drain_guard_count++;
            }
            break;
        }

        default:
            g_bsp_tfmini_uart_diag.unexpected_iidx_count++;
            break;
    }
    g_bsp_tfmini_uart_diag.irq_exit_count++;
}
