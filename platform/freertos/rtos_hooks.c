#include "rtos_hooks.h"

#include "FreeRTOS.h"
#include "task.h"

static StaticTask_t s_idle_task_tcb;
static StackType_t s_idle_task_stack[configIDLE_TASK_STACK_DEPTH];

#if (configUSE_TIMERS == 1)
static StaticTask_t s_timer_task_tcb;
static StackType_t s_timer_task_stack[configTIMER_TASK_STACK_DEPTH];
#endif

__attribute__((weak)) void RtosFault_EmergencyStop(void)
{
}

static void RtosFault_PrepareHalt(void)
{
    taskDISABLE_INTERRUPTS();
    RtosFault_EmergencyStop();
}

__attribute__((noreturn, noinline)) static void RtosFault_Loop(void)
{
    for (;;) {
        __asm volatile("nop");
    }
}

void RtosFault_Halt(rtos_fault_code_t code, TaskHandle_t task,
    const char *task_name, int line)
{
    RtosFault_PrepareHalt();
    RtosDiagnostics_RecordFault(code, task, task_name, NULL, line);
    RtosFault_Loop();
}

void RtosFault_Assert(const char *file, int line)
{
    RtosFault_PrepareHalt();
    RtosDiagnostics_RecordFault(
        RTOS_FAULT_ASSERT, NULL, NULL, file, line);
    RtosFault_Loop();
}

void vApplicationMallocFailedHook(void)
{
    RtosFault_PrepareHalt();
    RtosDiagnostics_RecordFault(
        RTOS_FAULT_MALLOC_FAILED, NULL, NULL, NULL, 0);
    RtosFault_Loop();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    RtosFault_PrepareHalt();
    RtosDiagnostics_RecordFault(
        RTOS_FAULT_STACK_OVERFLOW, task, task_name, NULL, 0);
    RtosFault_Loop();
}

void vApplicationGetIdleTaskMemory(StaticTask_t **task_tcb,
    StackType_t **task_stack, configSTACK_DEPTH_TYPE *stack_size)
{
    *task_tcb = &s_idle_task_tcb;
    *task_stack = s_idle_task_stack;
    *stack_size = configIDLE_TASK_STACK_DEPTH;
}

#if (configUSE_TIMERS == 1)
void vApplicationGetTimerTaskMemory(StaticTask_t **task_tcb,
    StackType_t **task_stack, configSTACK_DEPTH_TYPE *stack_size)
{
    *task_tcb = &s_timer_task_tcb;
    *task_stack = s_timer_task_stack;
    *stack_size = configTIMER_TASK_STACK_DEPTH;
}
#endif
