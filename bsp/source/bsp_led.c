#include "bsp_led.h"

#include "ti_msp_dl_config.h"

void BSP_LED_Toggle(void)
{
    DL_GPIO_togglePins(GPIO_LEDS_PORT, GPIO_LEDS_USER_LED_1_PIN);
}
