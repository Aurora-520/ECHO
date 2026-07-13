#ifndef ECHO_RTOS_HOOKS_H
#define ECHO_RTOS_HOOKS_H

#include "rtos_diagnostics.h"

#if defined(__GNUC__) || defined(__clang__) || defined(__ARMCC_VERSION)
#define RTOS_NORETURN __attribute__((noreturn))
#else
#define RTOS_NORETURN
#endif

RTOS_NORETURN void RtosFault_Assert(const char *file, int line);
RTOS_NORETURN void RtosFault_Halt(rtos_fault_code_t code, TaskHandle_t task,
    const char *task_name, int line);

#endif
