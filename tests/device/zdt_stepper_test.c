#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bsp_zdt_uart.h"
#include "zdt_stepper.h"

#define TEST_TX_CAPACITY 32U

typedef struct {
    bsp_zdt_uart_port_t port;
    uint8_t data[BSP_ZDT_UART_MAX_TX_BYTES];
    uint8_t length;
} test_tx_record_t;

volatile bsp_zdt_uart_diagnostics_t
    g_bsp_zdt_uart_diag[BSP_ZDT_UART_COUNT];
static test_tx_record_t s_tx[TEST_TX_CAPACITY];
static uint32_t s_tx_count;

void BSP_ZdtUart_Init(void)
{
    memset((void *) g_bsp_zdt_uart_diag, 0,
        sizeof(g_bsp_zdt_uart_diag));
    memset(s_tx, 0, sizeof(s_tx));
    s_tx_count = 0U;
}

bool BSP_ZdtUart_TryWrite(bsp_zdt_uart_port_t port,
    const uint8_t *data, uint8_t length)
{
    assert(s_tx_count < TEST_TX_CAPACITY);
    s_tx[s_tx_count].port = port;
    s_tx[s_tx_count].length = length;
    memcpy(s_tx[s_tx_count].data, data, length);
    s_tx_count++;
    return true;
}

bool BSP_ZdtUart_TryRead(bsp_zdt_uart_port_t port, uint8_t *byte)
{
    (void) port;
    (void) byte;
    return false;
}

uint16_t BSP_ZdtUart_Available(bsp_zdt_uart_port_t port)
{
    (void) port;
    return 0U;
}

void BSP_ZdtUart_FlushRx(bsp_zdt_uart_port_t port)
{
    (void) port;
}

bool BSP_ZdtUart_IsTxIdle(bsp_zdt_uart_port_t port)
{
    (void) port;
    return true;
}

static uint32_t CountFunction(uint8_t function)
{
    uint32_t count = 0U;
    uint32_t index;

    for (index = 0U; index < s_tx_count; index++) {
        if (s_tx[index].data[1] == function) {
            count++;
        }
    }
    return count;
}

static void TestDefaultIsSilent(void)
{
    ZdtStepper_Init();
    ZdtStepper_Service(20000U);
    ZdtStepper_Service(100000U);
    assert(s_tx_count == 0U);
    assert(g_zdt_stepper_diag.backend_selected == 0U);
    assert(ZdtStepper_RequestSpeed(
        ZDT_STEPPER_AXIS_GEN1, 100, 500U) ==
        ZDT_STEPPER_REQUEST_DISABLED);
}

static void TestSpeedDeduplication(void)
{
    ZdtStepper_Init();
    assert(ZdtStepper_SelectBackupBackend());
    assert(ZdtStepper_RequestSpeed(
        ZDT_STEPPER_AXIS_GEN1, -1000, 500U) ==
        ZDT_STEPPER_REQUEST_ACCEPTED);
    ZdtStepper_Service(20000U);
    assert(CountFunction(0xF6U) == 1U);
    assert(s_tx[0].port == BSP_ZDT_UART_GEN1);
    assert(s_tx[0].data[2] == 1U);
    assert(s_tx[0].data[3] == 0x03U);
    assert(s_tx[0].data[4] == 0xE8U);
    assert(s_tx[0].data[5] == 0xD8U);

    assert(ZdtStepper_RequestSpeed(
        ZDT_STEPPER_AXIS_GEN1, -1000, 500U) ==
        ZDT_STEPPER_REQUEST_DUPLICATE);
    ZdtStepper_Service(40000U);
    ZdtStepper_Service(100000U);
    assert(CountFunction(0xF6U) == 1U);

    assert(ZdtStepper_RequestSpeed(
        ZDT_STEPPER_AXIS_GEN2, 0, 500U) ==
        ZDT_STEPPER_REQUEST_ACCEPTED);
}

static void TestGenerationPositionPolicy(void)
{
    ZdtStepper_Init();
    assert(ZdtStepper_SelectBackupBackend());
    assert(ZdtStepper_RequestPosition(ZDT_STEPPER_AXIS_GEN1,
        90000, 100U, 500U, ZDT_POSITION_ABSOLUTE) ==
        ZDT_STEPPER_REQUEST_ACCEPTED);
    ZdtStepper_Service(20000U);
    assert(ZdtStepper_RequestPosition(ZDT_STEPPER_AXIS_GEN1,
        180000, 100U, 500U, ZDT_POSITION_ABSOLUTE) ==
        ZDT_STEPPER_REQUEST_BUSY);

    assert(ZdtStepper_RequestPosition(ZDT_STEPPER_AXIS_GEN2,
        90000, 100U, 500U, ZDT_POSITION_ABSOLUTE) ==
        ZDT_STEPPER_REQUEST_ACCEPTED);
    ZdtStepper_Service(40000U);
    assert(ZdtStepper_RequestPosition(ZDT_STEPPER_AXIS_GEN2,
        180000, 100U, 500U, ZDT_POSITION_ABSOLUTE) ==
        ZDT_STEPPER_REQUEST_ACCEPTED);
}

static void TestSpeedLeaseStopsMotor(void)
{
    ZdtStepper_Init();
    assert(ZdtStepper_SelectBackupBackend());
    assert(ZdtStepper_RequestSpeed(
        ZDT_STEPPER_AXIS_GEN2, 30, 500U) ==
        ZDT_STEPPER_REQUEST_ACCEPTED);
    ZdtStepper_Service(20000U);
    assert(CountFunction(0xF6U) == 1U);

    ZdtStepper_Service(1520000U);
    assert(CountFunction(0xFEU) == 1U);
    assert(g_zdt_stepper_diag.axis[ZDT_STEPPER_AXIS_GEN2]
        .speed_lease_expired_count == 1U);
}

static void TestDeselectStopsAndDisables(void)
{
    uint32_t index;
    uint32_t stop_count = 0U;
    uint32_t disable_count = 0U;

    ZdtStepper_Init();
    assert(ZdtStepper_SelectBackupBackend());
    ZdtStepper_DeselectBackupBackend();
    ZdtStepper_Service(20000U);
    ZdtStepper_Service(40000U);

    for (index = 0U; index < s_tx_count; index++) {
        if (s_tx[index].data[1] == 0xFEU) {
            stop_count++;
        } else if ((s_tx[index].data[1] == 0xF3U) &&
                   (s_tx[index].data[3] == 0U)) {
            disable_count++;
        }
    }
    assert(stop_count == ZDT_STEPPER_AXIS_COUNT);
    assert(disable_count == ZDT_STEPPER_AXIS_COUNT);
    assert(g_zdt_stepper_diag.backend_selected == 0U);
    assert(g_zdt_stepper_diag.shutdown_pending == 0U);
}

int main(void)
{
    TestDefaultIsSilent();
    TestSpeedDeduplication();
    TestGenerationPositionPolicy();
    TestSpeedLeaseStopsMotor();
    TestDeselectStopsAndDisables();
    return 0;
}
