/*
 * FreeRTOS configuration for the EtherCAT charger user application.
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include "board.h"

#if (portasmHAS_MTIME == 0)
#define configMTIME_BASE_ADDRESS                (0)
#define configMTIMECMP_BASE_ADDRESS             (0)
#else
#define configMTIME_BASE_ADDRESS                (HPM_MCHTMR_BASE)
#define configMTIMECMP_BASE_ADDRESS             (HPM_MCHTMR_BASE + 8UL)
#endif

#define configMAX_SYSCALL_INTERRUPT_PRIORITY    2
#define configUSE_PREEMPTION                    1
#define configCPU_CLOCK_HZ                      ((uint32_t) 24000000)
#define configTICK_RATE_HZ                      ((TickType_t) 1000)
#define configMINIMAL_STACK_SIZE                (1024)
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 0
#define configUSE_APPLICATION_TASK_TAG          0
#define configUSE_RECURSIVE_MUTEXES             1
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configTOTAL_HEAP_SIZE                   ((size_t) (96 * 1024))

#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCHECK_FOR_STACK_OVERFLOW          0
#define configUSE_MALLOC_FAILED_HOOK            0
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0
#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    0

#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskCleanUpResources           1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xTaskAbortDelay                 1
#define INCLUDE_xTaskGetHandle                  1
#define INCLUDE_xSemaphoreGetMutexHolder        1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_uxTaskGetStackHighWaterMark     1

#define configUSE_COUNTING_SEMAPHORES           1
#define configMAX_PRIORITIES                    56
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_MUTEXES                       1
#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         2

#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                4
#define configTIMER_TASK_STACK_DEPTH            (configMINIMAL_STACK_SIZE)

#define configASSERT(x) if ((x) == 0) { taskDISABLE_INTERRUPTS(); __asm volatile("ebreak"); for (;;); }

#define configCOMMAND_INT_MAX_OUTPUT_SIZE       2096
#define configINCLUDE_QUERY_HEAP_COMMAND        1

#ifndef __ASSEMBLER__
void vAssertCalled(const char *pcFile, unsigned long ulLine);
void vConfigureTickInterrupt(void);
void vClearTickInterrupt(void);
void vPreSleepProcessing(unsigned long uxExpectedIdleTime);
void vPostSleepProcessing(unsigned long uxExpectedIdleTime);
#endif

#define configSETUP_TICK_INTERRUPT()            vConfigureTickInterrupt()
#define configCLEAR_TICK_INTERRUPT()            vClearTickInterrupt()
#define configPRE_SLEEP_PROCESSING(x)           vPreSleepProcessing(x)
#define configPOST_SLEEP_PROCESSING(x)          vPostSleepProcessing(x)

#define fabs(x)                                 __builtin_fabs(x)
#define configHSP_ENABLE                        0

#if (configHSP_ENABLE == 1 && configRECORD_STACK_HIGH_ADDRESS != 1)
#define configRECORD_STACK_HIGH_ADDRESS         1
#endif

#endif
