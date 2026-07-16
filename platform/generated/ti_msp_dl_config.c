/*
 * Copyright (c) 2023, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *  ============ ti_msp_dl_config.c =============
 *  Configured MSPM0 DriverLib module definitions
 *
 *  DO NOT EDIT - This file is generated for the MSPM0G350X
 *  by the SysConfig tool.
 */

#include "ti_msp_dl_config.h"

DL_TimerA_backupConfig gCHASSIS_PWMBackup;
DL_TimerG_backupConfig gLEFT_ENCODER_QEIBackup;
DL_UART_Main_backupConfig gZDT_GEN2_UARTBackup;

/*
 *  ======== SYSCFG_DL_init ========
 *  Perform any initialization needed before using any board APIs
 */
SYSCONFIG_WEAK void SYSCFG_DL_init(void)
{
    SYSCFG_DL_initPower();
    SYSCFG_DL_GPIO_init();
    /* Module-Specific Initializations*/
    SYSCFG_DL_SYSCTL_init();
    SYSCFG_DL_CHASSIS_PWM_init();
    SYSCFG_DL_LEFT_ENCODER_QEI_init();
    SYSCFG_DL_TIMEBASE_init();
    SYSCFG_DL_OLED_I2C_init();
    SYSCFG_DL_DEBUG_UART_init();
    SYSCFG_DL_ZDT_GEN1_UART_init();
    SYSCFG_DL_ZDT_GEN2_UART_init();
    SYSCFG_DL_DMA_init();
    SYSCFG_DL_SYSCTL_CLK_init();
    /* Ensure backup structures have no valid state */
	gCHASSIS_PWMBackup.backupRdy 	= false;
	gLEFT_ENCODER_QEIBackup.backupRdy 	= false;

	gZDT_GEN2_UARTBackup.backupRdy 	= false;

}
/*
 * User should take care to save and restore register configuration in application.
 * See Retention Configuration section for more details.
 */
SYSCONFIG_WEAK bool SYSCFG_DL_saveConfiguration(void)
{
    bool retStatus = true;

	retStatus &= DL_TimerA_saveConfiguration(CHASSIS_PWM_INST, &gCHASSIS_PWMBackup);
	retStatus &= DL_TimerG_saveConfiguration(LEFT_ENCODER_QEI_INST, &gLEFT_ENCODER_QEIBackup);
	retStatus &= DL_UART_Main_saveConfiguration(ZDT_GEN2_UART_INST, &gZDT_GEN2_UARTBackup);

    return retStatus;
}


SYSCONFIG_WEAK bool SYSCFG_DL_restoreConfiguration(void)
{
    bool retStatus = true;

	retStatus &= DL_TimerA_restoreConfiguration(CHASSIS_PWM_INST, &gCHASSIS_PWMBackup, false);
	retStatus &= DL_TimerG_restoreConfiguration(LEFT_ENCODER_QEI_INST, &gLEFT_ENCODER_QEIBackup, false);
	retStatus &= DL_UART_Main_restoreConfiguration(ZDT_GEN2_UART_INST, &gZDT_GEN2_UARTBackup);

    return retStatus;
}

SYSCONFIG_WEAK void SYSCFG_DL_initPower(void)
{
    DL_GPIO_reset(GPIOA);
    DL_GPIO_reset(GPIOB);
    DL_TimerA_reset(CHASSIS_PWM_INST);
    DL_TimerG_reset(LEFT_ENCODER_QEI_INST);
    DL_TimerG_reset(TIMEBASE_INST);
    DL_I2C_reset(OLED_I2C_INST);
    DL_UART_Main_reset(DEBUG_UART_INST);
    DL_UART_Main_reset(ZDT_GEN1_UART_INST);
    DL_UART_Main_reset(ZDT_GEN2_UART_INST);


    DL_GPIO_enablePower(GPIOA);
    DL_GPIO_enablePower(GPIOB);
    DL_TimerA_enablePower(CHASSIS_PWM_INST);
    DL_TimerG_enablePower(LEFT_ENCODER_QEI_INST);
    DL_TimerG_enablePower(TIMEBASE_INST);
    DL_I2C_enablePower(OLED_I2C_INST);
    DL_UART_Main_enablePower(DEBUG_UART_INST);
    DL_UART_Main_enablePower(ZDT_GEN1_UART_INST);
    DL_UART_Main_enablePower(ZDT_GEN2_UART_INST);

    delay_cycles(POWER_STARTUP_DELAY);
}

SYSCONFIG_WEAK void SYSCFG_DL_GPIO_init(void)
{

    DL_GPIO_initPeripheralOutputFunction(GPIO_CHASSIS_PWM_C0_IOMUX,GPIO_CHASSIS_PWM_C0_IOMUX_FUNC);
    DL_GPIO_enableOutput(GPIO_CHASSIS_PWM_C0_PORT, GPIO_CHASSIS_PWM_C0_PIN);
    DL_GPIO_initPeripheralOutputFunction(GPIO_CHASSIS_PWM_C1_IOMUX,GPIO_CHASSIS_PWM_C1_IOMUX_FUNC);
    DL_GPIO_enableOutput(GPIO_CHASSIS_PWM_C1_PORT, GPIO_CHASSIS_PWM_C1_PIN);
    DL_GPIO_initPeripheralOutputFunction(GPIO_CHASSIS_PWM_C2_IOMUX,GPIO_CHASSIS_PWM_C2_IOMUX_FUNC);
    DL_GPIO_enableOutput(GPIO_CHASSIS_PWM_C2_PORT, GPIO_CHASSIS_PWM_C2_PIN);
    DL_GPIO_initPeripheralOutputFunction(GPIO_CHASSIS_PWM_C3_IOMUX,GPIO_CHASSIS_PWM_C3_IOMUX_FUNC);
    DL_GPIO_enableOutput(GPIO_CHASSIS_PWM_C3_PORT, GPIO_CHASSIS_PWM_C3_PIN);

    DL_GPIO_initPeripheralInputFunction(GPIO_LEFT_ENCODER_QEI_PHA_IOMUX,GPIO_LEFT_ENCODER_QEI_PHA_IOMUX_FUNC);
    DL_GPIO_initPeripheralInputFunction(GPIO_LEFT_ENCODER_QEI_PHB_IOMUX,GPIO_LEFT_ENCODER_QEI_PHB_IOMUX_FUNC);

    
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

    DL_GPIO_initPeripheralOutputFunction(
        GPIO_DEBUG_UART_IOMUX_TX, GPIO_DEBUG_UART_IOMUX_TX_FUNC);
    DL_GPIO_initPeripheralInputFunction(
        GPIO_DEBUG_UART_IOMUX_RX, GPIO_DEBUG_UART_IOMUX_RX_FUNC);
    DL_GPIO_initPeripheralOutputFunction(
        GPIO_ZDT_GEN1_UART_IOMUX_TX, GPIO_ZDT_GEN1_UART_IOMUX_TX_FUNC);
    DL_GPIO_initPeripheralInputFunction(
        GPIO_ZDT_GEN1_UART_IOMUX_RX, GPIO_ZDT_GEN1_UART_IOMUX_RX_FUNC);
    DL_GPIO_initPeripheralOutputFunction(
        GPIO_ZDT_GEN2_UART_IOMUX_TX, GPIO_ZDT_GEN2_UART_IOMUX_TX_FUNC);
    DL_GPIO_initPeripheralInputFunction(
        GPIO_ZDT_GEN2_UART_IOMUX_RX, GPIO_ZDT_GEN2_UART_IOMUX_RX_FUNC);

    DL_GPIO_initDigitalOutputFeatures(GPIO_LEDS_USER_LED_1_IOMUX,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_PULL_DOWN,
		 DL_GPIO_DRIVE_STRENGTH_LOW, DL_GPIO_HIZ_DISABLE);

    DL_GPIO_initDigitalInputFeatures(GPIO_RIGHT_ENCODER_RIGHT_ENCODER_E2A_IOMUX,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_NONE,
		 DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);

    DL_GPIO_initDigitalInputFeatures(GPIO_RIGHT_ENCODER_RIGHT_ENCODER_E2B_IOMUX,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_NONE,
		 DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);

    DL_GPIO_setPins(GPIOB, GPIO_LEDS_USER_LED_1_PIN);
    DL_GPIO_enableOutput(GPIOB, GPIO_LEDS_USER_LED_1_PIN);
    DL_GPIO_setLowerPinsPolarity(GPIOB, DL_GPIO_PIN_6_EDGE_RISE);
    DL_GPIO_clearInterruptStatus(GPIOB, GPIO_RIGHT_ENCODER_RIGHT_ENCODER_E2A_PIN);
    DL_GPIO_enableInterrupt(GPIOB, GPIO_RIGHT_ENCODER_RIGHT_ENCODER_E2A_PIN);

}


static const DL_SYSCTL_SYSPLLConfig gSYSPLLConfig = {
    .inputFreq              = DL_SYSCTL_SYSPLL_INPUT_FREQ_16_32_MHZ,
	.rDivClk2x              = 3,
	.rDivClk1               = 1,
	.rDivClk0               = 0,
	.enableCLK2x            = DL_SYSCTL_SYSPLL_CLK2X_ENABLE,
	.enableCLK1             = DL_SYSCTL_SYSPLL_CLK1_DISABLE,
	.enableCLK0             = DL_SYSCTL_SYSPLL_CLK0_DISABLE,
	.sysPLLMCLK             = DL_SYSCTL_SYSPLL_MCLK_CLK2X,
	.sysPLLRef              = DL_SYSCTL_SYSPLL_REF_SYSOSC,
	.qDiv                   = 9,
	.pDiv                   = DL_SYSCTL_SYSPLL_PDIV_2
};

SYSCONFIG_WEAK bool SYSCFG_DL_SYSCTL_SYSPLL_init(void)
{
    bool fFCCRatioStatus = false;
    uint32_t fFCCSysoscCount;
    uint32_t fFCCPllCount;
    uint32_t fFCCRatio;
    uint32_t fccTimeOutCounter;

    DL_SYSCTL_setFCCPeriods( DL_SYSCTL_FCC_TRIG_CNT_01 );

    /* Measuring PLL. */
    DL_SYSCTL_configFCC(DL_SYSCTL_FCC_TRIG_TYPE_RISE_RISE,
                        DL_SYSCTL_FCC_TRIG_SOURCE_LFCLK,
                        DL_SYSCTL_FCC_CLOCK_SOURCE_SYSPLLCLK2X);
    /* Get SYSPLL frequency using FCC */
    fccTimeOutCounter = 0;
    DL_SYSCTL_startFCC();
    while (DL_SYSCTL_isFCCDone() == 0) {
        delay_cycles(977);  /* 1x LFCLK cycle = 32MHz/32.768kHz = 977, 30.5us */
        fccTimeOutCounter++;
        if(fccTimeOutCounter > 65){
            /* Timeout set to approximately 2ms (user-customizable) */
            break;
        }
    }

    /* get measA= SYSPLLCLK2X freq wrt LFOSC*/
    fFCCPllCount = DL_SYSCTL_readFCC();

    /* Measuring SYSPLL Source */
    DL_SYSCTL_configFCC(DL_SYSCTL_FCC_TRIG_TYPE_RISE_RISE,
                        DL_SYSCTL_FCC_TRIG_SOURCE_LFCLK,
                        DL_SYSCTL_FCC_CLOCK_SOURCE_SYSOSC);
    /* Get SYSPLL frequency using FCC */
    fccTimeOutCounter = 0;
    DL_SYSCTL_startFCC();
    while (DL_SYSCTL_isFCCDone() == 0) {
        delay_cycles(977);  /* 1x LFCLK cycle = 32MHz/32.768kHz = 977, 30.5us */
        fccTimeOutCounter++;
        if(fccTimeOutCounter > 65){
            /* Timeout set to approximately 2ms (user-customizable) */
            break;
        }
    }

    /* get measB= SYSOSC freq wrt LFOSC*/
    fFCCSysoscCount = DL_SYSCTL_readFCC();

    /* Get ratio of both measurements*/
    fFCCRatio = (fFCCPllCount * FLOAT_TO_INT_SCALE) / fFCCSysoscCount;
    /* Check ratio is within bounds*/
    if ((FCC_LOWER_BOUND <  fFCCRatio) && (fFCCRatio < FCC_UPPER_BOUND))
    {
        /* ratio is good for proceeding into application code. */
        fFCCRatioStatus = true;
    }

    return fFCCRatioStatus;
}
SYSCONFIG_WEAK void SYSCFG_DL_SYSCTL_init(void)
{

	//Low Power Mode is configured to be SLEEP0
    DL_SYSCTL_setBORThreshold(DL_SYSCTL_BOR_THRESHOLD_LEVEL_0);
    DL_SYSCTL_setFlashWaitState(DL_SYSCTL_FLASH_WAIT_STATE_2);

    
	DL_SYSCTL_setSYSOSCFreq(DL_SYSCTL_SYSOSC_FREQ_BASE);
	/* Set default configuration */
	DL_SYSCTL_disableHFXT();
	DL_SYSCTL_disableSYSPLL();
    DL_SYSCTL_configSYSPLL((DL_SYSCTL_SYSPLLConfig *) &gSYSPLLConfig);

    /*
     * [SYSPLL_ERR_01]
     * PLL Incorrect locking WA start.
     * Insert after every PLL enable.
     * This can lead an infinite loop if the condition persists
     * and can block entry to the application code.
     */

    while (SYSCFG_DL_SYSCTL_SYSPLL_init() == false)
    {
        /* Toggle SYSPLL enable to re-enable SYSPLL and re-check incorrect locking */
        DL_SYSCTL_disableSYSPLL();
        DL_SYSCTL_enableSYSPLL();

        /* Wait until SYSPLL startup is stabilized*/
        while ((DL_SYSCTL_getClockStatus() & SYSCTL_CLKSTATUS_SYSPLLGOOD_MASK) != DL_SYSCTL_CLK_STATUS_SYSPLL_GOOD){}
    }
    DL_SYSCTL_setULPCLKDivider(DL_SYSCTL_ULPCLK_DIV_2);
    DL_SYSCTL_enableMFCLK();
    DL_SYSCTL_setMCLKSource(SYSOSC, HSCLK, DL_SYSCTL_HSCLK_SOURCE_SYSPLL);

}
SYSCONFIG_WEAK void SYSCFG_DL_SYSCTL_CLK_init(void) {
    while ((DL_SYSCTL_getClockStatus() & (DL_SYSCTL_CLK_STATUS_SYSPLL_GOOD
		 | DL_SYSCTL_CLK_STATUS_HSCLK_GOOD
		 | DL_SYSCTL_CLK_STATUS_LFOSC_GOOD))
	       != (DL_SYSCTL_CLK_STATUS_SYSPLL_GOOD
		 | DL_SYSCTL_CLK_STATUS_HSCLK_GOOD
		 | DL_SYSCTL_CLK_STATUS_LFOSC_GOOD))
	{
		/* Ensure that clocks are in default POR configuration before initialization.
		* Additionally once LFXT is enabled, the internal LFOSC is disabled, and cannot
		* be re-enabled other than by executing a BOOTRST. */
		;
	}
}



/*
 * Timer clock configuration to be sourced by  / 1 (4000000 Hz)
 * timerClkFreq = (timerClkSrc / (timerClkDivRatio * (timerClkPrescale + 1)))
 *   4000000 Hz = 4000000 Hz / (1 * (0 + 1))
 */
static const DL_TimerA_ClockConfig gCHASSIS_PWMClockConfig = {
    .clockSel = DL_TIMER_CLOCK_MFCLK,
    .divideRatio = DL_TIMER_CLOCK_DIVIDE_1,
    .prescale = 0U
};

static const DL_TimerA_PWMConfig gCHASSIS_PWMConfig = {
    .pwmMode = DL_TIMER_PWM_MODE_EDGE_ALIGN,
    .period = 399,
    .isTimerWithFourCC = true,
    .startTimer = DL_TIMER_STOP,
};

SYSCONFIG_WEAK void SYSCFG_DL_CHASSIS_PWM_init(void) {

    DL_TimerA_setClockConfig(
        CHASSIS_PWM_INST, (DL_TimerA_ClockConfig *) &gCHASSIS_PWMClockConfig);

    DL_TimerA_initPWMMode(
        CHASSIS_PWM_INST, (DL_TimerA_PWMConfig *) &gCHASSIS_PWMConfig);

    // Set Counter control to the smallest CC index being used
    DL_TimerA_setCounterControl(CHASSIS_PWM_INST,DL_TIMER_CZC_CCCTL0_ZCOND,DL_TIMER_CAC_CCCTL0_ACOND,DL_TIMER_CLC_CCCTL0_LCOND);

    DL_TimerA_setCaptureCompareOutCtl(CHASSIS_PWM_INST, DL_TIMER_CC_OCTL_INIT_VAL_LOW,
		DL_TIMER_CC_OCTL_INV_OUT_DISABLED, DL_TIMER_CC_OCTL_SRC_FUNCVAL,
		DL_TIMERA_CAPTURE_COMPARE_0_INDEX);

    DL_TimerA_setCaptCompUpdateMethod(CHASSIS_PWM_INST, DL_TIMER_CC_UPDATE_METHOD_ZERO_EVT, DL_TIMERA_CAPTURE_COMPARE_0_INDEX);
    DL_TimerA_setCaptureCompareValue(CHASSIS_PWM_INST, 399, DL_TIMER_CC_0_INDEX);

    DL_TimerA_setCaptureCompareOutCtl(CHASSIS_PWM_INST, DL_TIMER_CC_OCTL_INIT_VAL_LOW,
		DL_TIMER_CC_OCTL_INV_OUT_DISABLED, DL_TIMER_CC_OCTL_SRC_FUNCVAL,
		DL_TIMERA_CAPTURE_COMPARE_1_INDEX);

    DL_TimerA_setCaptCompUpdateMethod(CHASSIS_PWM_INST, DL_TIMER_CC_UPDATE_METHOD_ZERO_EVT, DL_TIMERA_CAPTURE_COMPARE_1_INDEX);
    DL_TimerA_setCaptureCompareValue(CHASSIS_PWM_INST, 399, DL_TIMER_CC_1_INDEX);

    DL_TimerA_setCaptureCompareOutCtl(CHASSIS_PWM_INST, DL_TIMER_CC_OCTL_INIT_VAL_LOW,
		DL_TIMER_CC_OCTL_INV_OUT_DISABLED, DL_TIMER_CC_OCTL_SRC_FUNCVAL,
		DL_TIMERA_CAPTURE_COMPARE_2_INDEX);

    DL_TimerA_setCaptCompUpdateMethod(CHASSIS_PWM_INST, DL_TIMER_CC_UPDATE_METHOD_ZERO_EVT, DL_TIMERA_CAPTURE_COMPARE_2_INDEX);
    DL_TimerA_setCaptureCompareValue(CHASSIS_PWM_INST, 399, DL_TIMER_CC_2_INDEX);

    DL_TimerA_setCaptureCompareOutCtl(CHASSIS_PWM_INST, DL_TIMER_CC_OCTL_INIT_VAL_LOW,
		DL_TIMER_CC_OCTL_INV_OUT_DISABLED, DL_TIMER_CC_OCTL_SRC_FUNCVAL,
		DL_TIMERA_CAPTURE_COMPARE_3_INDEX);

    DL_TimerA_setCaptCompUpdateMethod(CHASSIS_PWM_INST, DL_TIMER_CC_UPDATE_METHOD_ZERO_EVT, DL_TIMERA_CAPTURE_COMPARE_3_INDEX);
    DL_TimerA_setCaptureCompareValue(CHASSIS_PWM_INST, 399, DL_TIMER_CC_3_INDEX);

    DL_TimerA_enableClock(CHASSIS_PWM_INST);


    
    DL_TimerA_setCCPDirection(CHASSIS_PWM_INST , DL_TIMER_CC0_OUTPUT | DL_TIMER_CC1_OUTPUT | DL_TIMER_CC2_OUTPUT | DL_TIMER_CC3_OUTPUT );
    DL_TimerA_enableShadowFeatures(CHASSIS_PWM_INST);


}


static const DL_TimerG_ClockConfig gLEFT_ENCODER_QEIClockConfig = {
    .clockSel = DL_TIMER_CLOCK_BUSCLK,
    .divideRatio = DL_TIMER_CLOCK_DIVIDE_1,
    .prescale = 0U
};


SYSCONFIG_WEAK void SYSCFG_DL_LEFT_ENCODER_QEI_init(void) {

    DL_TimerG_setClockConfig(
        LEFT_ENCODER_QEI_INST, (DL_TimerG_ClockConfig *) &gLEFT_ENCODER_QEIClockConfig);

    DL_TimerG_configQEI(LEFT_ENCODER_QEI_INST, DL_TIMER_QEI_MODE_2_INPUT,
        DL_TIMER_CC_INPUT_INV_NOINVERT, DL_TIMER_CC_0_INDEX);
    DL_TimerG_configQEI(LEFT_ENCODER_QEI_INST, DL_TIMER_QEI_MODE_2_INPUT,
        DL_TIMER_CC_INPUT_INV_NOINVERT, DL_TIMER_CC_1_INDEX);
    DL_TimerG_setLoadValue(LEFT_ENCODER_QEI_INST, 65535);
    DL_TimerG_enableClock(LEFT_ENCODER_QEI_INST);
}



/*
 * Timer clock configuration to be sourced by MFCLK /  (1000000 Hz)
 * timerClkFreq = (timerClkSrc / (timerClkDivRatio * (timerClkPrescale + 1)))
 *   1000000 Hz = 1000000 Hz / (4 * (0 + 1))
 */
static const DL_TimerG_ClockConfig gTIMEBASEClockConfig = {
    .clockSel    = DL_TIMER_CLOCK_MFCLK,
    .divideRatio = DL_TIMER_CLOCK_DIVIDE_4,
    .prescale    = 0U,
};

/*
 * Timer load value (where the counter starts from) is calculated as (timerPeriod * timerClockFreq) - 1
 * TIMEBASE_INST_LOAD_VALUE = (4294.967296 s * 1000000 Hz) - 1
 */
static const DL_TimerG_TimerConfig gTIMEBASETimerConfig = {
    .period     = TIMEBASE_INST_LOAD_VALUE,
    .timerMode  = DL_TIMER_TIMER_MODE_PERIODIC_UP,
    .startTimer = DL_TIMER_STOP,
};

SYSCONFIG_WEAK void SYSCFG_DL_TIMEBASE_init(void) {

    DL_TimerG_setClockConfig(TIMEBASE_INST,
        (DL_TimerG_ClockConfig *) &gTIMEBASEClockConfig);

    DL_TimerG_initTimerMode(TIMEBASE_INST,
        (DL_TimerG_TimerConfig *) &gTIMEBASETimerConfig);
    DL_TimerG_enableClock(TIMEBASE_INST);





}


static const DL_I2C_ClockConfig gOLED_I2CClockConfig = {
    .clockSel = DL_I2C_CLOCK_BUSCLK,
    .divideRatio = DL_I2C_CLOCK_DIVIDE_1,
};

SYSCONFIG_WEAK void SYSCFG_DL_OLED_I2C_init(void) {

    DL_I2C_setClockConfig(OLED_I2C_INST,
        (DL_I2C_ClockConfig *) &gOLED_I2CClockConfig);
    DL_I2C_setAnalogGlitchFilterPulseWidth(OLED_I2C_INST,
        DL_I2C_ANALOG_GLITCH_FILTER_WIDTH_50NS);
    DL_I2C_enableAnalogGlitchFilter(OLED_I2C_INST);

    /* Configure Controller Mode */
    DL_I2C_resetControllerTransfer(OLED_I2C_INST);
    /* Set frequency to 400000 Hz*/
    DL_I2C_setTimerPeriod(OLED_I2C_INST, 9);
    DL_I2C_setControllerTXFIFOThreshold(OLED_I2C_INST, DL_I2C_TX_FIFO_LEVEL_EMPTY);
    DL_I2C_setControllerRXFIFOThreshold(OLED_I2C_INST, DL_I2C_RX_FIFO_LEVEL_BYTES_1);
    DL_I2C_enableControllerClockStretching(OLED_I2C_INST);


    /* Enable module */
    DL_I2C_enableController(OLED_I2C_INST);


}

static const DL_UART_Main_ClockConfig gDEBUG_UARTClockConfig = {
    .clockSel    = DL_UART_MAIN_CLOCK_BUSCLK,
    .divideRatio = DL_UART_MAIN_CLOCK_DIVIDE_RATIO_1
};

static const DL_UART_Main_Config gDEBUG_UARTConfig = {
    .mode        = DL_UART_MAIN_MODE_NORMAL,
    .direction   = DL_UART_MAIN_DIRECTION_TX_RX,
    .flowControl = DL_UART_MAIN_FLOW_CONTROL_NONE,
    .parity      = DL_UART_MAIN_PARITY_NONE,
    .wordLength  = DL_UART_MAIN_WORD_LENGTH_8_BITS,
    .stopBits    = DL_UART_MAIN_STOP_BITS_ONE
};

SYSCONFIG_WEAK void SYSCFG_DL_DEBUG_UART_init(void)
{
    DL_UART_Main_setClockConfig(DEBUG_UART_INST, (DL_UART_Main_ClockConfig *) &gDEBUG_UARTClockConfig);

    DL_UART_Main_init(DEBUG_UART_INST, (DL_UART_Main_Config *) &gDEBUG_UARTConfig);
    /*
     * Configure baud rate by setting oversampling and baud rate divisors.
     *  Target baud rate: 230400
     *  Actual baud rate: 230547.55
     */
    DL_UART_Main_setOversampling(DEBUG_UART_INST, DL_UART_OVERSAMPLING_RATE_16X);
    DL_UART_Main_setBaudRateDivisor(DEBUG_UART_INST, DEBUG_UART_IBRD_40_MHZ_230400_BAUD, DEBUG_UART_FBRD_40_MHZ_230400_BAUD);


    /* Configure Interrupts */
    DL_UART_Main_enableInterrupt(DEBUG_UART_INST,
                                 DL_UART_MAIN_INTERRUPT_DMA_DONE_TX |
                                 DL_UART_MAIN_INTERRUPT_EOT_DONE |
                                 DL_UART_MAIN_INTERRUPT_RX);

    /* Configure DMA Transmit Event */
    DL_UART_Main_enableDMATransmitEvent(DEBUG_UART_INST);
    /* Configure FIFOs */
    DL_UART_Main_enableFIFOs(DEBUG_UART_INST);
    DL_UART_Main_setRXFIFOThreshold(DEBUG_UART_INST, DL_UART_RX_FIFO_LEVEL_ONE_ENTRY);
    DL_UART_Main_setTXFIFOThreshold(DEBUG_UART_INST, DL_UART_TX_FIFO_LEVEL_ONE_ENTRY);

    DL_UART_Main_enable(DEBUG_UART_INST);
}
static const DL_UART_Main_ClockConfig gZDT_GEN1_UARTClockConfig = {
    .clockSel    = DL_UART_MAIN_CLOCK_BUSCLK,
    .divideRatio = DL_UART_MAIN_CLOCK_DIVIDE_RATIO_1
};

static const DL_UART_Main_Config gZDT_GEN1_UARTConfig = {
    .mode        = DL_UART_MAIN_MODE_NORMAL,
    .direction   = DL_UART_MAIN_DIRECTION_TX_RX,
    .flowControl = DL_UART_MAIN_FLOW_CONTROL_NONE,
    .parity      = DL_UART_MAIN_PARITY_NONE,
    .wordLength  = DL_UART_MAIN_WORD_LENGTH_8_BITS,
    .stopBits    = DL_UART_MAIN_STOP_BITS_ONE
};

SYSCONFIG_WEAK void SYSCFG_DL_ZDT_GEN1_UART_init(void)
{
    DL_UART_Main_setClockConfig(ZDT_GEN1_UART_INST, (DL_UART_Main_ClockConfig *) &gZDT_GEN1_UARTClockConfig);

    DL_UART_Main_init(ZDT_GEN1_UART_INST, (DL_UART_Main_Config *) &gZDT_GEN1_UARTConfig);
    /*
     * Configure baud rate by setting oversampling and baud rate divisors.
     *  Target baud rate: 115200
     *  Actual baud rate: 115190.78
     */
    DL_UART_Main_setOversampling(ZDT_GEN1_UART_INST, DL_UART_OVERSAMPLING_RATE_16X);
    DL_UART_Main_setBaudRateDivisor(ZDT_GEN1_UART_INST, ZDT_GEN1_UART_IBRD_40_MHZ_115200_BAUD, ZDT_GEN1_UART_FBRD_40_MHZ_115200_BAUD);


    /* Configure Interrupts */
    DL_UART_Main_enableInterrupt(ZDT_GEN1_UART_INST,
                                 DL_UART_MAIN_INTERRUPT_DMA_DONE_TX |
                                 DL_UART_MAIN_INTERRUPT_EOT_DONE |
                                 DL_UART_MAIN_INTERRUPT_RX);

    /* Configure DMA Transmit Event */
    DL_UART_Main_enableDMATransmitEvent(ZDT_GEN1_UART_INST);
    /* Configure FIFOs */
    DL_UART_Main_enableFIFOs(ZDT_GEN1_UART_INST);
    DL_UART_Main_setRXFIFOThreshold(ZDT_GEN1_UART_INST, DL_UART_RX_FIFO_LEVEL_ONE_ENTRY);
    DL_UART_Main_setTXFIFOThreshold(ZDT_GEN1_UART_INST, DL_UART_TX_FIFO_LEVEL_ONE_ENTRY);

    DL_UART_Main_enable(ZDT_GEN1_UART_INST);
}
static const DL_UART_Main_ClockConfig gZDT_GEN2_UARTClockConfig = {
    .clockSel    = DL_UART_MAIN_CLOCK_BUSCLK,
    .divideRatio = DL_UART_MAIN_CLOCK_DIVIDE_RATIO_1
};

static const DL_UART_Main_Config gZDT_GEN2_UARTConfig = {
    .mode        = DL_UART_MAIN_MODE_NORMAL,
    .direction   = DL_UART_MAIN_DIRECTION_TX_RX,
    .flowControl = DL_UART_MAIN_FLOW_CONTROL_NONE,
    .parity      = DL_UART_MAIN_PARITY_NONE,
    .wordLength  = DL_UART_MAIN_WORD_LENGTH_8_BITS,
    .stopBits    = DL_UART_MAIN_STOP_BITS_ONE
};

SYSCONFIG_WEAK void SYSCFG_DL_ZDT_GEN2_UART_init(void)
{
    DL_UART_Main_setClockConfig(ZDT_GEN2_UART_INST, (DL_UART_Main_ClockConfig *) &gZDT_GEN2_UARTClockConfig);

    DL_UART_Main_init(ZDT_GEN2_UART_INST, (DL_UART_Main_Config *) &gZDT_GEN2_UARTConfig);
    /*
     * Configure baud rate by setting oversampling and baud rate divisors.
     *  Target baud rate: 115200
     *  Actual baud rate: 115190.78
     */
    DL_UART_Main_setOversampling(ZDT_GEN2_UART_INST, DL_UART_OVERSAMPLING_RATE_16X);
    DL_UART_Main_setBaudRateDivisor(ZDT_GEN2_UART_INST, ZDT_GEN2_UART_IBRD_80_MHZ_115200_BAUD, ZDT_GEN2_UART_FBRD_80_MHZ_115200_BAUD);


    /* Configure Interrupts */
    DL_UART_Main_enableInterrupt(ZDT_GEN2_UART_INST,
                                 DL_UART_MAIN_INTERRUPT_DMA_DONE_TX |
                                 DL_UART_MAIN_INTERRUPT_EOT_DONE |
                                 DL_UART_MAIN_INTERRUPT_RX);

    /* Configure DMA Transmit Event */
    DL_UART_Main_enableDMATransmitEvent(ZDT_GEN2_UART_INST);
    /* Configure FIFOs */
    DL_UART_Main_enableFIFOs(ZDT_GEN2_UART_INST);
    DL_UART_Main_setRXFIFOThreshold(ZDT_GEN2_UART_INST, DL_UART_RX_FIFO_LEVEL_ONE_ENTRY);
    DL_UART_Main_setTXFIFOThreshold(ZDT_GEN2_UART_INST, DL_UART_TX_FIFO_LEVEL_ONE_ENTRY);

    DL_UART_Main_enable(ZDT_GEN2_UART_INST);
}

static const DL_DMA_Config gDEBUG_UART_TX_DMAConfig = {
    .transferMode   = DL_DMA_SINGLE_TRANSFER_MODE,
    .extendedMode   = DL_DMA_NORMAL_MODE,
    .destIncrement  = DL_DMA_ADDR_UNCHANGED,
    .srcIncrement   = DL_DMA_ADDR_INCREMENT,
    .destWidth      = DL_DMA_WIDTH_BYTE,
    .srcWidth       = DL_DMA_WIDTH_BYTE,
    .trigger        = DEBUG_UART_INST_DMA_TRIGGER,
    .triggerType    = DL_DMA_TRIGGER_TYPE_EXTERNAL,
};

SYSCONFIG_WEAK void SYSCFG_DL_DEBUG_UART_TX_DMA_init(void)
{
    DL_DMA_initChannel(DMA, DEBUG_UART_TX_DMA_CHAN_ID , (DL_DMA_Config *) &gDEBUG_UART_TX_DMAConfig);
}
static const DL_DMA_Config gZDT_GEN1_UART_TX_DMAConfig = {
    .transferMode   = DL_DMA_SINGLE_TRANSFER_MODE,
    .extendedMode   = DL_DMA_NORMAL_MODE,
    .destIncrement  = DL_DMA_ADDR_UNCHANGED,
    .srcIncrement   = DL_DMA_ADDR_INCREMENT,
    .destWidth      = DL_DMA_WIDTH_BYTE,
    .srcWidth       = DL_DMA_WIDTH_BYTE,
    .trigger        = ZDT_GEN1_UART_INST_DMA_TRIGGER,
    .triggerType    = DL_DMA_TRIGGER_TYPE_EXTERNAL,
};

SYSCONFIG_WEAK void SYSCFG_DL_ZDT_GEN1_UART_TX_DMA_init(void)
{
    DL_DMA_initChannel(DMA, ZDT_GEN1_UART_TX_DMA_CHAN_ID , (DL_DMA_Config *) &gZDT_GEN1_UART_TX_DMAConfig);
}
static const DL_DMA_Config gZDT_GEN2_UART_TX_DMAConfig = {
    .transferMode   = DL_DMA_SINGLE_TRANSFER_MODE,
    .extendedMode   = DL_DMA_NORMAL_MODE,
    .destIncrement  = DL_DMA_ADDR_UNCHANGED,
    .srcIncrement   = DL_DMA_ADDR_INCREMENT,
    .destWidth      = DL_DMA_WIDTH_BYTE,
    .srcWidth       = DL_DMA_WIDTH_BYTE,
    .trigger        = ZDT_GEN2_UART_INST_DMA_TRIGGER,
    .triggerType    = DL_DMA_TRIGGER_TYPE_EXTERNAL,
};

SYSCONFIG_WEAK void SYSCFG_DL_ZDT_GEN2_UART_TX_DMA_init(void)
{
    DL_DMA_initChannel(DMA, ZDT_GEN2_UART_TX_DMA_CHAN_ID , (DL_DMA_Config *) &gZDT_GEN2_UART_TX_DMAConfig);
}
SYSCONFIG_WEAK void SYSCFG_DL_DMA_init(void){
    SYSCFG_DL_DEBUG_UART_TX_DMA_init();
    SYSCFG_DL_ZDT_GEN1_UART_TX_DMA_init();
    SYSCFG_DL_ZDT_GEN2_UART_TX_DMA_init();
}


