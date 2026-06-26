#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "app_log.h"
#include "board.h"
#include "charger_app.h"
#include "csh.h"
#include "hpm_ota.h"
#include "portable.h"
#include "task.h"

#define CHARGER_CONTROL_MASK (0x007FU)
#define PS_DEFAULT_INTERVAL_MS (200U)
#define PS_MAX_INTERVAL_MS     (10000U)
#define TOP_DEFAULT_INTERVAL_MS (1000U)
#define TOP_DEFAULT_COUNT       (0U)
#define TOP_MAX_COUNT           (1000U)

static chry_shell_t *shell_from_argv(int argc, char **argv)
{
    return (chry_shell_t *)argv[argc + 1];
}

static bool parse_u16(const char *text, uint16_t *value)
{
    char *end = NULL;
    unsigned long parsed;

    if ((text == NULL) || (value == NULL) || (text[0] == '\0')) {
        return false;
    }

    errno = 0;
    parsed = strtoul(text, &end, 0);
    if ((errno != 0) || (end == text) || (*end != '\0') || (parsed > 0xFFFFUL)) {
        return false;
    }

    *value = (uint16_t)parsed;
    return true;
}

static bool parse_u32_range(const char *text, uint32_t min_value, uint32_t max_value, uint32_t *value)
{
    char *end = NULL;
    unsigned long parsed;

    if ((text == NULL) || (value == NULL) || (text[0] == '\0')) {
        return false;
    }

    errno = 0;
    parsed = strtoul(text, &end, 0);
    if ((errno != 0) || (end == text) || (*end != '\0') ||
        (parsed < min_value) || (parsed > max_value)) {
        return false;
    }

    *value = (uint32_t)parsed;
    return true;
}

static char task_state_char(eTaskState state)
{
    switch (state) {
    case eRunning:
        return 'R';
    case eReady:
        return 'Y';
    case eBlocked:
        return 'B';
    case eSuspended:
        return 'S';
    case eDeleted:
        return 'D';
    default:
        return '?';
    }
}

static TaskStatus_t *find_task_by_number(TaskStatus_t *tasks, UBaseType_t task_count, UBaseType_t task_number)
{
    for (UBaseType_t i = 0; i < task_count; i++) {
        if (tasks[i].xTaskNumber == task_number) {
            return &tasks[i];
        }
    }

    return NULL;
}

static int task_cpu_compare_desc(const void *lhs, const void *rhs)
{
    const TaskStatus_t *left = (const TaskStatus_t *)lhs;
    const TaskStatus_t *right = (const TaskStatus_t *)rhs;

    if (left->ulRunTimeCounter < right->ulRunTimeCounter) {
        return 1;
    }
    if (left->ulRunTimeCounter > right->ulRunTimeCounter) {
        return -1;
    }
    return 0;
}

static int capture_tasks(TaskStatus_t **tasks, UBaseType_t *task_count, configRUN_TIME_COUNTER_TYPE *total_time)
{
    UBaseType_t capacity = uxTaskGetNumberOfTasks() + 4U;
    TaskStatus_t *snapshot = pvPortMalloc(capacity * sizeof(TaskStatus_t));

    if (snapshot == NULL) {
        return -1;
    }

    *task_count = uxTaskGetSystemState(snapshot, capacity, total_time);
    if (*task_count > capacity) {
        vPortFree(snapshot);
        return -1;
    }

    *tasks = snapshot;
    return 0;
}

static void print_ps_header(chry_shell_t *csh,
                            uint32_t interval_ms,
                            UBaseType_t task_count,
                            configRUN_TIME_COUNTER_TYPE total_delta)
{
    csh_printf(csh,
               "Tasks:%u  interval:%ums  heap:%u/%u bytes  ticks:%u\r\n",
               (unsigned int)task_count,
               (unsigned int)interval_ms,
               (unsigned int)xPortGetFreeHeapSize(),
               (unsigned int)xPortGetMinimumEverFreeHeapSize(),
               (unsigned int)total_delta);
    csh_printf(csh, "PID  S PRI STACK CPU%%   RUN(ms) NAME\r\n");
    csh_printf(csh, "---- - --- ----- ------ -------- ----------------\r\n");
}

static int print_ps_sample(chry_shell_t *csh, uint32_t interval_ms, bool refresh_screen)
{
    TaskStatus_t *first_tasks = NULL;
    TaskStatus_t *second_tasks = NULL;
    UBaseType_t first_count = 0;
    UBaseType_t second_count = 0;
    configRUN_TIME_COUNTER_TYPE first_total = 0;
    configRUN_TIME_COUNTER_TYPE second_total = 0;
    configRUN_TIME_COUNTER_TYPE total_delta;
    int ret = 0;

    if (capture_tasks(&first_tasks, &first_count, &first_total) != 0) {
        csh_printf(csh, "no memory for task snapshot\r\n");
        return -1;
    }

    vTaskDelay(pdMS_TO_TICKS(interval_ms));

    if (capture_tasks(&second_tasks, &second_count, &second_total) != 0) {
        csh_printf(csh, "no memory for task snapshot\r\n");
        ret = -1;
        goto out;
    }

    total_delta = second_total - first_total;
    for (UBaseType_t i = 0; i < second_count; i++) {
        TaskStatus_t *previous = find_task_by_number(first_tasks, first_count, second_tasks[i].xTaskNumber);

        if (previous != NULL) {
            second_tasks[i].ulRunTimeCounter -= previous->ulRunTimeCounter;
        }
    }

    qsort(second_tasks, second_count, sizeof(TaskStatus_t), task_cpu_compare_desc);
    if (refresh_screen) {
        csh_printf(csh, "\033[H\033[J");
        csh_printf(csh, "top - %ums refresh\r\n", (unsigned int)interval_ms);
    }
    print_ps_header(csh, interval_ms, second_count, total_delta);

    for (UBaseType_t i = 0; i < second_count; i++) {
        configRUN_TIME_COUNTER_TYPE runtime_total = second_tasks[i].ulRunTimeCounter;
        uint64_t cpu_x10 = 0;
        uint32_t time_ms;

        if (total_delta > 0U) {
            cpu_x10 = ((uint64_t)runtime_total * 1000ULL) / (uint64_t)total_delta;
        }
        time_ms = (uint32_t)(((uint64_t)runtime_total + 12000ULL) / 24000ULL);

        csh_printf(csh,
                   "%4u %c %3u %5u %3u.%u %8u %-16s\r\n",
                   (unsigned int)second_tasks[i].xTaskNumber,
                   task_state_char(second_tasks[i].eCurrentState),
                   (unsigned int)second_tasks[i].uxCurrentPriority,
                   (unsigned int)second_tasks[i].usStackHighWaterMark,
                   (unsigned int)(cpu_x10 / 10U),
                   (unsigned int)(cpu_x10 % 10U),
                   (unsigned int)time_ms,
                   second_tasks[i].pcTaskName);
    }

out:
    if (second_tasks != NULL) {
        vPortFree(second_tasks);
    }
    vPortFree(first_tasks);
    return ret;
}

static int shell_status(int argc, char **argv)
{
    chry_shell_t *csh = shell_from_argv(argc, argv);
    charger_txpdo_t txpdo;

    (void)argc;

    charger_app_get_txpdo(&txpdo);
    csh_printf(csh, "status:0x%04x\r\n", txpdo.status_word);
    csh_printf(csh, "soc:%u\r\n", txpdo.battery_level_x100);
    csh_printf(csh, "sys_input:%umV\r\n", txpdo.sys_input_voltage_mv);
    csh_printf(csh, "battery:%umV\r\n", txpdo.battery_voltage_mv);
    csh_printf(csh, "charge:%umA\r\n", txpdo.charge_current_ma);
    csh_printf(csh, "discharge:%umA\r\n", txpdo.discharge_current_ma);
    csh_printf(csh, "resistance:%umOhm\r\n", txpdo.internal_resistance_mohm);
    csh_printf(csh, "ota_app:%d\r\n", hpm_ota_get_nowrunning_app());

    return 0;
}
CSH_CMD_EXPORT_ALIAS(shell_status, status, );

static int shell_rxpdo(int argc, char **argv)
{
    chry_shell_t *csh = shell_from_argv(argc, argv);
    charger_rxpdo_t rxpdo;
    uint16_t control_word;

    charger_app_get_rxpdo(&rxpdo);

    if (argc == 1) {
        csh_printf(csh, "control_word:0x%04x\r\n", rxpdo.control_word);
        return 0;
    }

    if (argc != 2) {
        csh_printf(csh, "usage: rxpdo [control_word]\r\n");
        csh_printf(csh, "  control_word: 0x0000-0x007f\r\n");
        return -1;
    }

    if (!parse_u16(argv[1], &control_word)) {
        csh_printf(csh, "invalid control_word: %s\r\n", argv[1]);
        return -1;
    }

    if ((control_word & ~CHARGER_CONTROL_MASK) != 0U) {
        csh_printf(csh, "reserved bits are not allowed: 0x%04x\r\n", control_word);
        return -1;
    }

    rxpdo.control_word = control_word;
    charger_app_set_rxpdo(&rxpdo);
    csh_printf(csh, "control_word set to 0x%04x\r\n", rxpdo.control_word);

    return 0;
}
CSH_CMD_EXPORT_ALIAS(shell_rxpdo, rxpdo, );

static int shell_led(int argc, char **argv)
{
    chry_shell_t *csh = shell_from_argv(argc, argv);
    uint8_t off_level = board_get_led_gpio_off_level();

    if (argc != 2) {
        csh_printf(csh, "usage: led <0|1|toggle>\r\n");
        return -1;
    }

    if (strcmp(argv[1], "toggle") == 0) {
        board_led_toggle();
        csh_printf(csh, "led toggled\r\n");
        return 0;
    }

    if (strcmp(argv[1], "0") == 0) {
        board_led_write(off_level);
        csh_printf(csh, "led off\r\n");
        return 0;
    }

    if (strcmp(argv[1], "1") == 0) {
        board_led_write((uint8_t)!off_level);
        csh_printf(csh, "led on\r\n");
        return 0;
    }

    csh_printf(csh, "invalid led state: %s\r\n", argv[1]);
    return -1;
}
CSH_CMD_EXPORT_ALIAS(shell_led, led, );

static int shell_log(int argc, char **argv)
{
    chry_shell_t *csh = shell_from_argv(argc, argv);

    if (argc == 1 || ((argc == 2) && (strcmp(argv[1], "status") == 0))) {
        csh_printf(csh, "log:%s\r\n", app_log_is_enabled() ? "on" : "off");
        return 0;
    }

    if (argc != 2) {
        csh_printf(csh, "usage: log <on|off|status>\r\n");
        return -1;
    }

    if (strcmp(argv[1], "on") == 0) {
        app_log_set_enabled(true);
        csh_printf(csh, "log:on\r\n");
        return 0;
    }

    if (strcmp(argv[1], "off") == 0) {
        app_log_set_enabled(false);
        csh_printf(csh, "log:off\r\n");
        return 0;
    }

    csh_printf(csh, "invalid log state: %s\r\n", argv[1]);
    return -1;
}
CSH_CMD_EXPORT_ALIAS(shell_log, log, );

static int shell_heap(int argc, char **argv)
{
    chry_shell_t *csh = shell_from_argv(argc, argv);

    (void)argc;
    (void)argv;

    csh_printf(csh, "heap free:%u bytes\r\n", (unsigned int)xPortGetFreeHeapSize());
    csh_printf(csh, "heap min:%u bytes\r\n", (unsigned int)xPortGetMinimumEverFreeHeapSize());

    return 0;
}
CSH_CMD_EXPORT_ALIAS(shell_heap, heap, );

static int shell_tasks(int argc, char **argv)
{
    chry_shell_t *csh = shell_from_argv(argc, argv);
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    TaskStatus_t *tasks;
    UBaseType_t actual_count;

    (void)argc;
    (void)argv;

    tasks = pvPortMalloc(task_count * sizeof(TaskStatus_t));
    if (tasks == NULL) {
        csh_printf(csh, "no memory for %u task records\r\n", (unsigned int)task_count);
        return -1;
    }

    actual_count = uxTaskGetSystemState(tasks, task_count, NULL);

    csh_printf(csh, "name             st prio stack\r\n");
    csh_printf(csh, "---------------- -- ---- -----\r\n");
    for (UBaseType_t i = 0; i < actual_count; i++) {
        csh_printf(csh,
                   "%-16s %c  %4u %5u\r\n",
                   tasks[i].pcTaskName,
                   task_state_char(tasks[i].eCurrentState),
                   (unsigned int)tasks[i].uxCurrentPriority,
                   (unsigned int)tasks[i].usStackHighWaterMark);
    }

    vPortFree(tasks);
    return 0;
}
CSH_CMD_EXPORT_ALIAS(shell_tasks, tasks, );

static int shell_ps(int argc, char **argv)
{
    chry_shell_t *csh = shell_from_argv(argc, argv);
    uint32_t interval_ms = PS_DEFAULT_INTERVAL_MS;

    if (argc > 2) {
        csh_printf(csh, "usage: ps [interval_ms]\r\n");
        return -1;
    }

    if ((argc == 2) && !parse_u32_range(argv[1], 1U, PS_MAX_INTERVAL_MS, &interval_ms)) {
        csh_printf(csh, "invalid interval_ms: %s\r\n", argv[1]);
        csh_printf(csh, "  range: 1-%u\r\n", (unsigned int)PS_MAX_INTERVAL_MS);
        return -1;
    }

    return print_ps_sample(csh, interval_ms, false);
}
CSH_CMD_EXPORT_ALIAS(shell_ps, ps, );

static int shell_top(int argc, char **argv)
{
    chry_shell_t *csh = shell_from_argv(argc, argv);
    uint32_t interval_ms = TOP_DEFAULT_INTERVAL_MS;
    uint32_t count = TOP_DEFAULT_COUNT;
    uint32_t iteration = 0;

    if (argc > 3) {
        csh_printf(csh, "usage: top [interval_ms] [count]\r\n");
        csh_printf(csh, "  count 0 means continuous refresh\r\n");
        return -1;
    }

    if ((argc >= 2) && !parse_u32_range(argv[1], 1U, PS_MAX_INTERVAL_MS, &interval_ms)) {
        csh_printf(csh, "invalid interval_ms: %s\r\n", argv[1]);
        csh_printf(csh, "  range: 1-%u\r\n", (unsigned int)PS_MAX_INTERVAL_MS);
        return -1;
    }

    if ((argc == 3) && !parse_u32_range(argv[2], 0U, TOP_MAX_COUNT, &count)) {
        csh_printf(csh, "invalid count: %s\r\n", argv[2]);
        csh_printf(csh, "  range: 0-%u\r\n", (unsigned int)TOP_MAX_COUNT);
        return -1;
    }

    while ((count == 0U) || (iteration < count)) {
        iteration++;
        if (print_ps_sample(csh, interval_ms, true) != 0) {
            return -1;
        }
    }

    return 0;
}
CSH_CMD_EXPORT_ALIAS(shell_top, top, );
