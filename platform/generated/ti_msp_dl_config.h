/*
 * Copyright (c) 2023, Texas Instruments Incorporated - http://www.ti.com
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
 *  ============ ti_msp_dl_config.h =============
 *  Configured MSPM0 DriverLib module declarations
 *
 *  DO NOT EDIT - This file is generated for the MSPM0G350X
 *  by the SysConfig tool.
 */
#ifndef ti_msp_dl_config_h
#define ti_msp_dl_config_h

#define CONFIG_MSPM0G350X
#define CONFIG_MSPM0G3507

#if defined(__ti_version__) || defined(__TI_COMPILER_VERSION__)
#define SYSCONFIG_WEAK __attribute__((weak))
#elif defined(__IAR_SYSTEMS_ICC__)
#define SYSCONFIG_WEAK __weak
#elif defined(__GNUC__)
#define SYSCONFIG_WEAK __attribute__((weak))
#endif

#include <ti/devices/msp/msp.h>
#include <ti/driverlib/driverlib.h>
#include <ti/driverlib/m0p/dl_core.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  ======== SYSCFG_DL_init ========
 *  Perform all required MSP DL initialization
 *
 *  This function should be called once at a point before any use of
 *  MSP DL.
 */


/* clang-format off */

#define POWER_STARTUP_DELAY                                                (16)



#define CPUCLK_FREQ                                                     80000000
/* Defines for SYSPLL_ERR_01 Workaround */
/* Represent 1.000 as 1000 */
#define FLOAT_TO_INT_SCALE                                               (1000U)
#define FCC_EXPECTED_RATIO                                                  2500
#define FCC_UPPER_BOUND                       (FCC_EXPECTED_RATIO * (1 + 0.003))
#define FCC_LOWER_BOUND                       (FCC_EXPECTED_RATIO * (1 - 0.003))

bool SYSCFG_DL_SYSCTL_SYSPLL_init(void);


/* Defines for TIMEBASE */
#define TIMEBASE_INST                                                   (TIMG12)
#define TIMEBASE_INST_IRQHandler                               TIMG12_IRQHandler
#define TIMEBASE_INST_INT_IRQN                                 (TIMG12_INT_IRQn)
#define TIMEBASE_INST_LOAD_VALUE                                   (4294967295U)




/* Defines for OLED_I2C */
#define OLED_I2C_INST                                                       I2C0
#define OLED_I2C_INST_IRQHandler                                 I2C0_IRQHandler
#define OLED_I2C_INST_INT_IRQN                                     I2C0_INT_IRQn
#define OLED_I2C_BUS_SPEED_HZ                                             400000
#define GPIO_OLED_I2C_SDA_PORT                                             GPIOA
#define GPIO_OLED_I2C_SDA_PIN                                      DL_GPIO_PIN_0
#define GPIO_OLED_I2C_IOMUX_SDA                                   (IOMUX_PINCM1)
#define GPIO_OLED_I2C_IOMUX_SDA_FUNC                    IOMUX_PINCM1_PF_I2C0_SDA
#define GPIO_OLED_I2C_SCL_PORT                                             GPIOA
#define GPIO_OLED_I2C_SCL_PIN                                      DL_GPIO_PIN_1
#define GPIO_OLED_I2C_IOMUX_SCL                                   (IOMUX_PINCM2)
#define GPIO_OLED_I2C_IOMUX_SCL_FUNC                    IOMUX_PINCM2_PF_I2C0_SCL


/* Defines for DEBUG_UART */
#define DEBUG_UART_INST                                                    UART1
#define DEBUG_UART_INST_FREQUENCY                                       40000000
#define DEBUG_UART_INST_IRQHandler                              UART1_IRQHandler
#define DEBUG_UART_INST_INT_IRQN                                  UART1_INT_IRQn
#define GPIO_DEBUG_UART_RX_PORT                                            GPIOA
#define GPIO_DEBUG_UART_TX_PORT                                            GPIOA
#define GPIO_DEBUG_UART_RX_PIN                                     DL_GPIO_PIN_9
#define GPIO_DEBUG_UART_TX_PIN                                     DL_GPIO_PIN_8
#define GPIO_DEBUG_UART_IOMUX_RX                                 (IOMUX_PINCM20)
#define GPIO_DEBUG_UART_IOMUX_TX                                 (IOMUX_PINCM19)
#define GPIO_DEBUG_UART_IOMUX_RX_FUNC                  IOMUX_PINCM20_PF_UART1_RX
#define GPIO_DEBUG_UART_IOMUX_TX_FUNC                  IOMUX_PINCM19_PF_UART1_TX
#define DEBUG_UART_BAUD_RATE                                            (230400)
#define DEBUG_UART_IBRD_40_MHZ_230400_BAUD                                  (10)
#define DEBUG_UART_FBRD_40_MHZ_230400_BAUD                                  (54)





/* Defines for DEBUG_UART_TX_DMA */
#define DEBUG_UART_TX_DMA_CHAN_ID                                            (3)
#define DEBUG_UART_INST_DMA_TRIGGER                          (DMA_UART1_TX_TRIG)


/* Port definition for Pin Group GPIO_LEDS */
#define GPIO_LEDS_PORT                                                   (GPIOB)

/* Defines for USER_LED_1: GPIOB.22 with pinCMx 50 on package pin 21 */
#define GPIO_LEDS_USER_LED_1_PIN                                (DL_GPIO_PIN_22)
#define GPIO_LEDS_USER_LED_1_IOMUX                               (IOMUX_PINCM50)


/* clang-format on */

void SYSCFG_DL_init(void);
void SYSCFG_DL_initPower(void);
void SYSCFG_DL_GPIO_init(void);
void SYSCFG_DL_SYSCTL_init(void);
void SYSCFG_DL_SYSCTL_CLK_init(void);

bool SYSCFG_DL_SYSCTL_SYSPLL_init(void);
void SYSCFG_DL_TIMEBASE_init(void);
void SYSCFG_DL_OLED_I2C_init(void);
void SYSCFG_DL_DEBUG_UART_init(void);
void SYSCFG_DL_DMA_init(void);


bool SYSCFG_DL_saveConfiguration(void);
bool SYSCFG_DL_restoreConfiguration(void);

#ifdef __cplusplus
}
#endif

#endif /* ti_msp_dl_config_h */
