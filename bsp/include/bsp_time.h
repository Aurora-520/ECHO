#ifndef ECHO_BSP_TIME_H
#define ECHO_BSP_TIME_H

#include <stdint.h>

#define BSP_TIME_FREQUENCY_HZ 1000000UL

void BSP_Time_Init(void);
uint32_t BSP_Time_GetUs(void);

#endif
