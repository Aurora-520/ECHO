#include "bsp_i2c.h"

#include <stddef.h>

#include "FreeRTOS.h"
#include "bsp_time.h"
#include "semphr.h"
#include "ti_msp_dl_config.h"

#define BSP_I2C_READY_TIMEOUT_US    1000UL
#define BSP_I2C_TRANSFER_TIMEOUT_US 3000UL
#define BSP_I2C_FLUSH_TIMEOUT_US    100UL
#define BSP_I2C_BUS_CLEAR_PULSE_US 5UL
#define BSP_I2C_SCL_RELEASE_TIMEOUT_US 100UL
#define BSP_I2C_STATUS_POLL_DELAY_US 1UL
#define BSP_I2C_MUTEX_TIMEOUT       pdMS_TO_TICKS(5U)

volatile bsp_i2c_diagnostics_t g_bsp_i2c_diag;

static StaticSemaphore_t s_i2c_mutex_storage;
static SemaphoreHandle_t s_i2c_mutex;

static bool BSP_I2C_Elapsed(uint32_t start_us, uint32_t timeout_us)
{
    return ((uint32_t) (BSP_Time_GetUs() - start_us) >= timeout_us);
}

static bool BSP_I2C_FlushTxLocked(void)
{
    uint32_t start_us;

    DL_I2C_startFlushControllerTXFIFO(OLED_I2C_INST);
    start_us = BSP_Time_GetUs();
    while (!DL_I2C_isControllerTXFIFOEmpty(OLED_I2C_INST)) {
        if (BSP_I2C_Elapsed(start_us, BSP_I2C_FLUSH_TIMEOUT_US)) {
            DL_I2C_stopFlushControllerTXFIFO(OLED_I2C_INST);
            return false;
        }
    }
    DL_I2C_stopFlushControllerTXFIFO(OLED_I2C_INST);
    return true;
}

static void BSP_I2C_DrainRxLocked(void)
{
    while (!DL_I2C_isControllerRXFIFOEmpty(OLED_I2C_INST)) {
        (void) DL_I2C_receiveControllerData(OLED_I2C_INST);
    }
}

static void BSP_I2C_DelayUs(uint32_t delay_us)
{
    uint32_t start_us = BSP_Time_GetUs();

    while (!BSP_I2C_Elapsed(start_us, delay_us)) {
    }
}

static bool BSP_I2C_PinIsHigh(GPIO_Regs *port, uint32_t pin)
{
    return ((DL_GPIO_readPins(port, pin) & pin) != 0U);
}

static bool BSP_I2C_WaitPinHigh(
    GPIO_Regs *port, uint32_t pin, uint32_t timeout_us)
{
    uint32_t start_us = BSP_Time_GetUs();

    while (!BSP_I2C_PinIsHigh(port, pin)) {
        if (BSP_I2C_Elapsed(start_us, timeout_us)) {
            return false;
        }
    }
    return true;
}

static void BSP_I2C_RestorePeripheralPins(void)
{
    DL_GPIO_disableOutput(
        GPIO_OLED_I2C_SDA_PORT, GPIO_OLED_I2C_SDA_PIN);
    DL_GPIO_disableOutput(
        GPIO_OLED_I2C_SCL_PORT, GPIO_OLED_I2C_SCL_PIN);

    DL_GPIO_initPeripheralInputFunctionFeatures(
        GPIO_OLED_I2C_IOMUX_SDA, GPIO_OLED_I2C_IOMUX_SDA_FUNC,
        DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_NONE,
        DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);
    DL_GPIO_initPeripheralInputFunctionFeatures(
        GPIO_OLED_I2C_IOMUX_SCL, GPIO_OLED_I2C_IOMUX_SCL_FUNC,
        DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_NONE,
        DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);
    DL_GPIO_enableHiZ(GPIO_OLED_I2C_IOMUX_SDA);
    DL_GPIO_enableHiZ(GPIO_OLED_I2C_IOMUX_SCL);
    SYSCFG_DL_OLED_I2C_init();
}

static bool BSP_I2C_BusClearLocked(void)
{
    uint8_t pulse;
    bool success = false;

    g_bsp_i2c_diag.bus_clear_attempt_count++;
    DL_I2C_disableController(OLED_I2C_INST);

    DL_GPIO_disableOutput(
        GPIO_OLED_I2C_SDA_PORT, GPIO_OLED_I2C_SDA_PIN);
    DL_GPIO_disableOutput(
        GPIO_OLED_I2C_SCL_PORT, GPIO_OLED_I2C_SCL_PIN);
    DL_GPIO_clearPins(
        GPIO_OLED_I2C_SDA_PORT, GPIO_OLED_I2C_SDA_PIN);
    DL_GPIO_clearPins(
        GPIO_OLED_I2C_SCL_PORT, GPIO_OLED_I2C_SCL_PIN);
    DL_GPIO_initDigitalInputFeatures(GPIO_OLED_I2C_IOMUX_SDA,
        DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_NONE,
        DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);
    DL_GPIO_initDigitalInputFeatures(GPIO_OLED_I2C_IOMUX_SCL,
        DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_NONE,
        DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);
    BSP_I2C_DelayUs(BSP_I2C_BUS_CLEAR_PULSE_US);

    if (BSP_I2C_WaitPinHigh(GPIO_OLED_I2C_SCL_PORT,
            GPIO_OLED_I2C_SCL_PIN,
            BSP_I2C_SCL_RELEASE_TIMEOUT_US)) {
        for (pulse = 0U; pulse < 9U; pulse++) {
            if (BSP_I2C_PinIsHigh(
                    GPIO_OLED_I2C_SDA_PORT, GPIO_OLED_I2C_SDA_PIN)) {
                break;
            }

            DL_GPIO_enableOutput(
                GPIO_OLED_I2C_SCL_PORT, GPIO_OLED_I2C_SCL_PIN);
            BSP_I2C_DelayUs(BSP_I2C_BUS_CLEAR_PULSE_US);
            DL_GPIO_disableOutput(
                GPIO_OLED_I2C_SCL_PORT, GPIO_OLED_I2C_SCL_PIN);
            if (!BSP_I2C_WaitPinHigh(GPIO_OLED_I2C_SCL_PORT,
                    GPIO_OLED_I2C_SCL_PIN,
                    BSP_I2C_SCL_RELEASE_TIMEOUT_US)) {
                break;
            }
            BSP_I2C_DelayUs(BSP_I2C_BUS_CLEAR_PULSE_US);
        }

        DL_GPIO_enableOutput(
            GPIO_OLED_I2C_SDA_PORT, GPIO_OLED_I2C_SDA_PIN);
        BSP_I2C_DelayUs(BSP_I2C_BUS_CLEAR_PULSE_US);
        DL_GPIO_disableOutput(
            GPIO_OLED_I2C_SCL_PORT, GPIO_OLED_I2C_SCL_PIN);
        if (BSP_I2C_WaitPinHigh(GPIO_OLED_I2C_SCL_PORT,
                GPIO_OLED_I2C_SCL_PIN,
                BSP_I2C_SCL_RELEASE_TIMEOUT_US)) {
            BSP_I2C_DelayUs(BSP_I2C_BUS_CLEAR_PULSE_US);
            DL_GPIO_disableOutput(
                GPIO_OLED_I2C_SDA_PORT, GPIO_OLED_I2C_SDA_PIN);
            BSP_I2C_DelayUs(BSP_I2C_BUS_CLEAR_PULSE_US);
            success = BSP_I2C_PinIsHigh(
                    GPIO_OLED_I2C_SCL_PORT, GPIO_OLED_I2C_SCL_PIN) &&
                BSP_I2C_PinIsHigh(
                    GPIO_OLED_I2C_SDA_PORT, GPIO_OLED_I2C_SDA_PIN);
        }
    }

    BSP_I2C_RestorePeripheralPins();
    if (success) {
        g_bsp_i2c_diag.bus_clear_success_count++;
    } else {
        g_bsp_i2c_diag.bus_clear_failure_count++;
    }
    return success;
}

static void BSP_I2C_RecoverLocked(void)
{
    uint32_t status;

    DL_I2C_resetControllerTransfer(OLED_I2C_INST);
    (void) BSP_I2C_FlushTxLocked();
    DL_I2C_disableController(OLED_I2C_INST);
    BSP_I2C_DelayUs(BSP_I2C_STATUS_POLL_DELAY_US);
    DL_I2C_enableControllerClockStretching(OLED_I2C_INST);
    DL_I2C_enableController(OLED_I2C_INST);
    DL_I2C_resetControllerTransfer(OLED_I2C_INST);
    BSP_I2C_DelayUs(BSP_I2C_BUS_CLEAR_PULSE_US);

    status = DL_I2C_getControllerStatus(OLED_I2C_INST);
    if ((status & DL_I2C_CONTROLLER_STATUS_BUSY_BUS) != 0U) {
        (void) BSP_I2C_BusClearLocked();
    }
    g_bsp_i2c_diag.recovery_count++;
}

static bsp_i2c_result_t BSP_I2C_WaitReadyLocked(void)
{
    uint32_t start_us = BSP_Time_GetUs();

    for (;;) {
        uint32_t status = DL_I2C_getControllerStatus(OLED_I2C_INST);
        if (BSP_I2C_Elapsed(start_us, BSP_I2C_READY_TIMEOUT_US)) {
            return BSP_I2C_RESULT_BUS_BUSY_TIMEOUT;
        }

        g_bsp_i2c_diag.last_controller_status = status;
        if ((status & DL_I2C_CONTROLLER_STATUS_ARBITRATION_LOST) != 0U) {
            return BSP_I2C_RESULT_ARBITRATION_LOST;
        }
        if ((status & DL_I2C_CONTROLLER_STATUS_ERROR) != 0U) {
            BSP_I2C_RecoverLocked();
            continue;
        }
        if (((status & DL_I2C_CONTROLLER_STATUS_IDLE) != 0U) &&
            ((status & (DL_I2C_CONTROLLER_STATUS_BUSY |
                           DL_I2C_CONTROLLER_STATUS_BUSY_BUS)) == 0U)) {
            return BSP_I2C_RESULT_OK;
        }
    }
}

static bsp_i2c_result_t BSP_I2C_WriteLocked(
    uint8_t address, const uint8_t *data, uint16_t length)
{
    bsp_i2c_result_t result;
    uint16_t loaded;
    uint32_t start_us;

    result = BSP_I2C_WaitReadyLocked();
    if (result != BSP_I2C_RESULT_OK) {
        return result;
    }

    DL_I2C_resetControllerTransfer(OLED_I2C_INST);
    if (!BSP_I2C_FlushTxLocked()) {
        return BSP_I2C_RESULT_FIFO_ERROR;
    }

    loaded = DL_I2C_fillControllerTXFIFO(OLED_I2C_INST, data, length);
    if (loaded != length) {
        return BSP_I2C_RESULT_FIFO_ERROR;
    }

    DL_I2C_startControllerTransfer(OLED_I2C_INST, address,
        DL_I2C_CONTROLLER_DIRECTION_TX, length);

    /*
     * MSPM0 I2C errata requires at least three I2C functional clocks
     * before status polling. The 1 MHz timebase keeps this CPU-clock agnostic.
     */
    BSP_I2C_DelayUs(BSP_I2C_STATUS_POLL_DELAY_US);
    start_us = BSP_Time_GetUs();

    for (;;) {
        uint32_t status = DL_I2C_getControllerStatus(OLED_I2C_INST);

        g_bsp_i2c_diag.last_controller_status = status;
        if ((status & DL_I2C_CONTROLLER_STATUS_ARBITRATION_LOST) != 0U) {
            return BSP_I2C_RESULT_ARBITRATION_LOST;
        }
        if ((status & DL_I2C_CONTROLLER_STATUS_ERROR) != 0U) {
            return BSP_I2C_RESULT_NACK;
        }
        if (((status & DL_I2C_CONTROLLER_STATUS_BUSY) == 0U) &&
            ((status & DL_I2C_CONTROLLER_STATUS_BUSY_BUS) == 0U) &&
            ((status & DL_I2C_CONTROLLER_STATUS_IDLE) != 0U) &&
            (DL_I2C_getTransactionCount(OLED_I2C_INST) == 0U)) {
            return BSP_I2C_RESULT_OK;
        }
        if (BSP_I2C_Elapsed(start_us, BSP_I2C_TRANSFER_TIMEOUT_US)) {
            return BSP_I2C_RESULT_TRANSFER_TIMEOUT;
        }
    }
}

static bsp_i2c_result_t BSP_I2C_ReadLocked(
    uint8_t address, uint8_t *read_data, uint16_t read_length)
{
    bsp_i2c_result_t result;
    uint16_t received = 0U;
    uint32_t start_us;

    result = BSP_I2C_WaitReadyLocked();
    if (result != BSP_I2C_RESULT_OK) {
        return result;
    }

    DL_I2C_resetControllerTransfer(OLED_I2C_INST);
    BSP_I2C_DrainRxLocked();
    DL_I2C_startControllerTransfer(OLED_I2C_INST, address,
        DL_I2C_CONTROLLER_DIRECTION_RX, read_length);
    BSP_I2C_DelayUs(BSP_I2C_STATUS_POLL_DELAY_US);
    start_us = BSP_Time_GetUs();

    for (;;) {
        uint32_t status = DL_I2C_getControllerStatus(OLED_I2C_INST);

        g_bsp_i2c_diag.last_controller_status = status;
        if ((status & DL_I2C_CONTROLLER_STATUS_ARBITRATION_LOST) != 0U) {
            return BSP_I2C_RESULT_ARBITRATION_LOST;
        }
        if ((status & DL_I2C_CONTROLLER_STATUS_ERROR) != 0U) {
            return BSP_I2C_RESULT_NACK;
        }

        while (!DL_I2C_isControllerRXFIFOEmpty(OLED_I2C_INST)) {
            if (received >= read_length) {
                BSP_I2C_DrainRxLocked();
                return BSP_I2C_RESULT_FIFO_ERROR;
            }
            read_data[received] =
                DL_I2C_receiveControllerData(OLED_I2C_INST);
            received++;
        }

        if ((received == read_length) &&
            ((status & (DL_I2C_CONTROLLER_STATUS_BUSY |
                           DL_I2C_CONTROLLER_STATUS_BUSY_BUS)) == 0U) &&
            ((status & DL_I2C_CONTROLLER_STATUS_IDLE) != 0U)) {
            return BSP_I2C_RESULT_OK;
        }
        if (BSP_I2C_Elapsed(start_us, BSP_I2C_TRANSFER_TIMEOUT_US)) {
            return BSP_I2C_RESULT_TRANSFER_TIMEOUT;
        }
    }
}

static bsp_i2c_result_t BSP_I2C_ReadAfterWriteLocked(uint8_t address,
    const uint8_t *write_data, uint16_t write_length,
    uint8_t *read_data, uint16_t read_length)
{
    bsp_i2c_result_t result =
        BSP_I2C_WriteLocked(address, write_data, write_length);

    if (result != BSP_I2C_RESULT_OK) {
        return result;
    }
    return BSP_I2C_ReadLocked(address, read_data, read_length);
}

static void BSP_I2C_RecordFailure(bsp_i2c_result_t result)
{
    switch (result) {
        case BSP_I2C_RESULT_NACK:
            g_bsp_i2c_diag.nack_count++;
            break;
        case BSP_I2C_RESULT_ARBITRATION_LOST:
            g_bsp_i2c_diag.arbitration_lost_count++;
            break;
        case BSP_I2C_RESULT_BUS_BUSY_TIMEOUT:
            g_bsp_i2c_diag.bus_busy_timeout_count++;
            break;
        case BSP_I2C_RESULT_TRANSFER_TIMEOUT:
            g_bsp_i2c_diag.transfer_timeout_count++;
            break;
        case BSP_I2C_RESULT_FIFO_ERROR:
            g_bsp_i2c_diag.fifo_error_count++;
            break;
        default:
            break;
    }
}

void BSP_I2C_Init(void)
{
    g_bsp_i2c_diag.write_attempt_count = 0U;
    g_bsp_i2c_diag.write_success_count = 0U;
    g_bsp_i2c_diag.read_attempt_count = 0U;
    g_bsp_i2c_diag.read_success_count = 0U;
    g_bsp_i2c_diag.nack_count = 0U;
    g_bsp_i2c_diag.arbitration_lost_count = 0U;
    g_bsp_i2c_diag.bus_busy_timeout_count = 0U;
    g_bsp_i2c_diag.transfer_timeout_count = 0U;
    g_bsp_i2c_diag.mutex_timeout_count = 0U;
    g_bsp_i2c_diag.fifo_error_count = 0U;
    g_bsp_i2c_diag.recovery_count = 0U;
    g_bsp_i2c_diag.bus_clear_attempt_count = 0U;
    g_bsp_i2c_diag.bus_clear_success_count = 0U;
    g_bsp_i2c_diag.bus_clear_failure_count = 0U;
    g_bsp_i2c_diag.last_controller_status =
        DL_I2C_getControllerStatus(OLED_I2C_INST);
    g_bsp_i2c_diag.last_length = 0U;
    g_bsp_i2c_diag.last_read_length = 0U;
    g_bsp_i2c_diag.last_address = 0U;
    g_bsp_i2c_diag.last_result = (uint32_t) BSP_I2C_RESULT_OK;

    s_i2c_mutex = xSemaphoreCreateMutexStatic(&s_i2c_mutex_storage);
    g_bsp_i2c_diag.initialized = (s_i2c_mutex != NULL) ? 1U : 0U;
}

bsp_i2c_result_t BSP_I2C_Write(
    uint8_t address, const uint8_t *data, uint16_t length)
{
    bsp_i2c_result_t result;

    if (g_bsp_i2c_diag.initialized == 0U) {
        return BSP_I2C_RESULT_NOT_INITIALIZED;
    }
    if ((data == NULL) || (length == 0U) ||
        (length > BSP_I2C_MAX_WRITE_BYTES) || (address > 0x7FU)) {
        g_bsp_i2c_diag.last_result =
            (uint32_t) BSP_I2C_RESULT_INVALID_ARGUMENT;
        return BSP_I2C_RESULT_INVALID_ARGUMENT;
    }
    if (xSemaphoreTake(s_i2c_mutex, BSP_I2C_MUTEX_TIMEOUT) != pdPASS) {
        g_bsp_i2c_diag.mutex_timeout_count++;
        g_bsp_i2c_diag.last_result =
            (uint32_t) BSP_I2C_RESULT_MUTEX_TIMEOUT;
        return BSP_I2C_RESULT_MUTEX_TIMEOUT;
    }

    g_bsp_i2c_diag.write_attempt_count++;
    g_bsp_i2c_diag.last_address = address;
    g_bsp_i2c_diag.last_length = length;
    result = BSP_I2C_WriteLocked(address, data, length);

    if (result == BSP_I2C_RESULT_OK) {
        g_bsp_i2c_diag.write_success_count++;
    } else {
        BSP_I2C_RecordFailure(result);
        BSP_I2C_RecoverLocked();
    }

    g_bsp_i2c_diag.last_result = (uint32_t) result;
    (void) xSemaphoreGive(s_i2c_mutex);
    return result;
}

bsp_i2c_result_t BSP_I2C_WriteRead(uint8_t address,
    const uint8_t *write_data, uint16_t write_length,
    uint8_t *read_data, uint16_t read_length)
{
    bsp_i2c_result_t result;

    if (g_bsp_i2c_diag.initialized == 0U) {
        return BSP_I2C_RESULT_NOT_INITIALIZED;
    }
    if ((write_data == NULL) || (read_data == NULL) ||
        (write_length == 0U) ||
        (write_length > BSP_I2C_MAX_WRITE_BYTES) ||
        (read_length == 0U) || (read_length > BSP_I2C_MAX_READ_BYTES) ||
        (address > 0x7FU)) {
        g_bsp_i2c_diag.last_result =
            (uint32_t) BSP_I2C_RESULT_INVALID_ARGUMENT;
        return BSP_I2C_RESULT_INVALID_ARGUMENT;
    }
    if (xSemaphoreTake(s_i2c_mutex, BSP_I2C_MUTEX_TIMEOUT) != pdPASS) {
        g_bsp_i2c_diag.mutex_timeout_count++;
        g_bsp_i2c_diag.last_result =
            (uint32_t) BSP_I2C_RESULT_MUTEX_TIMEOUT;
        return BSP_I2C_RESULT_MUTEX_TIMEOUT;
    }

    g_bsp_i2c_diag.write_attempt_count++;
    g_bsp_i2c_diag.read_attempt_count++;
    g_bsp_i2c_diag.last_address = address;
    g_bsp_i2c_diag.last_length = write_length;
    g_bsp_i2c_diag.last_read_length = read_length;
    result = BSP_I2C_ReadAfterWriteLocked(address, write_data,
        write_length, read_data, read_length);

    if (result == BSP_I2C_RESULT_OK) {
        g_bsp_i2c_diag.write_success_count++;
        g_bsp_i2c_diag.read_success_count++;
    } else {
        BSP_I2C_RecordFailure(result);
        BSP_I2C_RecoverLocked();
    }

    g_bsp_i2c_diag.last_result = (uint32_t) result;
    (void) xSemaphoreGive(s_i2c_mutex);
    return result;
}

const volatile bsp_i2c_diagnostics_t *BSP_I2C_GetDiagnostics(void)
{
    return &g_bsp_i2c_diag;
}
