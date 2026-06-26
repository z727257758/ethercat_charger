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
#include "ecat_def.h"
#include "hpm_ota.h"
#include "modbus_master_app.h"
#include "portable.h"
#include "task.h"

#ifndef APP_VERSION_STRING
#define APP_VERSION_STRING "unknown"
#endif

#define CHARGER_CONTROL_MASK (0x007FU)
#define PS_DEFAULT_INTERVAL_MS (200U)
#define PS_MAX_INTERVAL_MS     (10000U)
#define TOP_DEFAULT_INTERVAL_MS (1000U)
#define TOP_DEFAULT_COUNT       (0U)
#define TOP_MAX_COUNT           (1000U)
#define WATCH_DEFAULT_INTERVAL_MS (1000U)
#define WATCH_DEFAULT_COUNT       (10U)
#define WATCH_MAX_COUNT           (1000U)

typedef struct {
    const char *name;
    const char *description;
    uint16_t mask;
} bit_desc_t;

typedef struct {
    const char *name;
    uint8_t bit;
} control_desc_t;

static const bit_desc_t s_status_bits[] = {
    { "charge_on", "charge enabled", (uint16_t)(1U << 0) },
    { "board_power", "switch board power", (uint16_t)(1U << 1) },
    { "resistor_discharge", "resistor discharge enabled", (uint16_t)(1U << 2) },
    { "charge_done", "charge complete", (uint16_t)(1U << 3) },
    { "discharge_done", "resistor discharge complete", (uint16_t)(1U << 4) },
    { "launch", "aircraft launch", (uint16_t)(1U << 5) },
    { "takeoff", "aircraft takeoff", (uint16_t)(1U << 6) },
    { "battery_connected", "aircraft battery connected", (uint16_t)(1U << 7) },
    { "bms", "BMS on", (uint16_t)(1U << 8) },
    { "binding", "binding success", (uint16_t)(1U << 9) },
    { "aircraft_connected", "aircraft connected", (uint16_t)(1U << 10) },
    { "charge_mode", "charge mode", (uint16_t)(1U << 11) },
    { "storage_mode", "storage mode", (uint16_t)(1U << 12) },
    { "reserved13", "reserved", (uint16_t)(1U << 13) },
    { "reserved14", "reserved", (uint16_t)(1U << 14) },
    { "reserved15", "reserved", (uint16_t)(1U << 15) },
};

static const control_desc_t s_control_bits[] = {
    { "charge", 0U },
    { "power", 1U },
    { "storage", 2U },
    { "resistance", 3U },
    { "launch", 4U },
    { "takeoff", 5U },
    { "bms", 6U },
};

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

static bool parse_on_off(const char *text, bool *value)
{
    if ((text == NULL) || (value == NULL)) {
        return false;
    }

    if ((strcmp(text, "on") == 0) || (strcmp(text, "1") == 0)) {
        *value = true;
        return true;
    }

    if ((strcmp(text, "off") == 0) || (strcmp(text, "0") == 0)) {
        *value = false;
        return true;
    }

    return false;
}

static const control_desc_t *find_control_desc(const char *name)
{
    for (uint32_t i = 0; i < (sizeof(s_control_bits) / sizeof(s_control_bits[0])); i++) {
        if (strcmp(name, s_control_bits[i].name) == 0) {
            return &s_control_bits[i];
        }
    }

    return NULL;
}

static const char *modbus_state_name(uint32_t state)
{
    static const char *const names[] = {
        "send_control",
        "wait_control_tx_done",
        "wait_control_response",
        "process_control_response",
        "send_status_read",
        "wait_status_tx_done",
        "wait_status_response",
        "process_status_response",
        "send_parameters_read",
        "wait_parameters_tx_done",
        "wait_parameters_response",
        "process_parameters_response",
        "poll_delay",
    };

    if (state < (sizeof(names) / sizeof(names[0]))) {
        return names[state];
    }

    return "unknown";
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

static void print_status_bits(chry_shell_t *csh, uint16_t status_word)
{
    csh_printf(csh, "status_word:0x%04x\r\n", (unsigned int)status_word);
    for (uint32_t i = 0; i < (sizeof(s_status_bits) / sizeof(s_status_bits[0])); i++) {
        csh_printf(csh,
                   "  bit%02u %-20s %u  %s\r\n",
                   (unsigned int)i,
                   s_status_bits[i].name,
                   (status_word & s_status_bits[i].mask) ? 1U : 0U,
                   s_status_bits[i].description);
    }
}

static void print_control_word(chry_shell_t *csh, uint16_t control_word)
{
    csh_printf(csh, "control_word:0x%04x\r\n", (unsigned int)control_word);
    for (uint32_t i = 0; i < (sizeof(s_control_bits) / sizeof(s_control_bits[0])); i++) {
        uint16_t mask = (uint16_t)(1U << s_control_bits[i].bit);

        csh_printf(csh,
                   "  %-10s bit%u:%u\r\n",
                   s_control_bits[i].name,
                   (unsigned int)s_control_bits[i].bit,
                   (unsigned int)((control_word & mask) ? 1U : 0U));
    }
}

static void print_charger_snapshot(chry_shell_t *csh, bool verbose)
{
    charger_rxpdo_t rxpdo;
    charger_txpdo_t txpdo;

    charger_app_get_rxpdo(&rxpdo);
    charger_app_get_txpdo(&txpdo);

    csh_printf(csh,
               "rx_control:0x%04x tx_status:0x%04x\r\n",
               (unsigned int)rxpdo.control_word,
               (unsigned int)txpdo.status_word);
    csh_printf(csh,
               "soc:%u.%02u%% sys:%umV bat:%umV\r\n",
               (unsigned int)(txpdo.battery_level_x100 / 100U),
               (unsigned int)(txpdo.battery_level_x100 % 100U),
               (unsigned int)txpdo.sys_input_voltage_mv,
               (unsigned int)txpdo.battery_voltage_mv);
    csh_printf(csh,
               "charge:%umA discharge:%umA resistance:%umOhm ota_app:%d\r\n",
               (unsigned int)txpdo.charge_current_ma,
               (unsigned int)txpdo.discharge_current_ma,
               (unsigned int)txpdo.internal_resistance_mohm,
               hpm_ota_get_nowrunning_app());

    if (verbose) {
        print_control_word(csh, rxpdo.control_word);
        print_status_bits(csh, txpdo.status_word);
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

static void sort_tasks_by_runtime(TaskStatus_t *tasks, UBaseType_t task_count)
{
    for (UBaseType_t i = 0; i < task_count; i++) {
        UBaseType_t max_index = i;

        for (UBaseType_t j = i + 1U; j < task_count; j++) {
            if (tasks[j].ulRunTimeCounter > tasks[max_index].ulRunTimeCounter) {
                max_index = j;
            }
        }

        if (max_index != i) {
            TaskStatus_t tmp = tasks[i];
            tasks[i] = tasks[max_index];
            tasks[max_index] = tmp;
        }
    }
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

    sort_tasks_by_runtime(second_tasks, second_count);
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

    if (argc == 1) {
        print_charger_snapshot(csh, false);
        return 0;
    }

    if ((argc == 2) && ((strcmp(argv[1], "-v") == 0) || (strcmp(argv[1], "bits") == 0))) {
        print_charger_snapshot(csh, true);
        return 0;
    }

    csh_printf(csh, "usage: status [-v|bits]\r\n");
    return -1;
}
CSH_CMD_EXPORT_ALIAS(shell_status, status, );

static int shell_ver(int argc, char **argv)
{
    chry_shell_t *csh = shell_from_argv(argc, argv);

    (void)argv;
    if (argc != 1) {
        csh_printf(csh, "usage: ver\r\n");
        return -1;
    }

    csh_printf(csh, "app:%s\r\n", APP_VERSION_STRING);
    csh_printf(csh, "build:%s %s\r\n", __DATE__, __TIME__);
    csh_printf(csh, "board:%s\r\n", BOARD_NAME);
    csh_printf(csh, "ota_app:%d\r\n", hpm_ota_get_nowrunning_app());
    csh_printf(csh, "ecat product:0x%08x revision:0x%08x\r\n",
               (unsigned int)PRODUCT_CODE,
               (unsigned int)REVISION_NUMBER);
    csh_printf(csh, "ssc:%u.%u\r\n", (unsigned int)SSC_VERSION_MAJOR, (unsigned int)SSC_VERSION_MINOR);

    return 0;
}
CSH_CMD_EXPORT_ALIAS(shell_ver, ver, );

static int shell_uptime(int argc, char **argv)
{
    chry_shell_t *csh = shell_from_argv(argc, argv);
    TickType_t ticks = xTaskGetTickCount();
    uint32_t seconds = (uint32_t)(ticks / configTICK_RATE_HZ);

    (void)argv;
    if (argc != 1) {
        csh_printf(csh, "usage: uptime\r\n");
        return -1;
    }

    csh_printf(csh, "uptime:%ud %02u:%02u:%02u\r\n",
               (unsigned int)(seconds / 86400U),
               (unsigned int)((seconds / 3600U) % 24U),
               (unsigned int)((seconds / 60U) % 60U),
               (unsigned int)(seconds % 60U));
    csh_printf(csh, "ticks:%u tick_rate:%uHz\r\n",
               (unsigned int)ticks,
               (unsigned int)configTICK_RATE_HZ);
    csh_printf(csh, "heap free:%u min:%u bytes\r\n",
               (unsigned int)xPortGetFreeHeapSize(),
               (unsigned int)xPortGetMinimumEverFreeHeapSize());

    return 0;
}
CSH_CMD_EXPORT_ALIAS(shell_uptime, uptime, );

static int shell_pdo(int argc, char **argv)
{
    chry_shell_t *csh = shell_from_argv(argc, argv);

    (void)argv;
    if (argc != 1) {
        csh_printf(csh, "usage: pdo\r\n");
        return -1;
    }

    print_charger_snapshot(csh, true);
    return 0;
}
CSH_CMD_EXPORT_ALIAS(shell_pdo, pdo, );

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

static int shell_control(int argc, char **argv)
{
    chry_shell_t *csh = shell_from_argv(argc, argv);
    charger_rxpdo_t rxpdo;
    uint16_t control_word;

    charger_app_get_rxpdo(&rxpdo);

    if ((argc == 1) || ((argc == 2) && (strcmp(argv[1], "show") == 0))) {
        print_control_word(csh, rxpdo.control_word);
        return 0;
    }

    if ((argc == 3) && (strcmp(argv[1], "raw") == 0)) {
        if (!parse_u16(argv[2], &control_word)) {
            csh_printf(csh, "invalid control_word: %s\r\n", argv[2]);
            return -1;
        }

        if ((control_word & ~CHARGER_CONTROL_MASK) != 0U) {
            csh_printf(csh, "reserved bits are not allowed: 0x%04x\r\n", control_word);
            return -1;
        }

        rxpdo.control_word = control_word;
        charger_app_set_rxpdo(&rxpdo);
        print_control_word(csh, rxpdo.control_word);
        return 0;
    }

    if (argc == 3) {
        const control_desc_t *desc = find_control_desc(argv[1]);
        bool enabled;
        uint16_t mask;

        if (desc == NULL) {
            csh_printf(csh, "unknown control: %s\r\n", argv[1]);
            csh_printf(csh, "valid: charge power storage resistance launch takeoff bms\r\n");
            return -1;
        }

        if (!parse_on_off(argv[2], &enabled)) {
            csh_printf(csh, "usage: control <%s> <on|off>\r\n", argv[1]);
            return -1;
        }

        mask = (uint16_t)(1U << desc->bit);
        if (enabled) {
            rxpdo.control_word |= mask;
        } else {
            rxpdo.control_word &= (uint16_t)~mask;
        }

        charger_app_set_rxpdo(&rxpdo);
        csh_printf(csh, "%s:%s\r\n", desc->name, enabled ? "on" : "off");
        print_control_word(csh, rxpdo.control_word);
        return 0;
    }

    csh_printf(csh, "usage: control [show]\r\n");
    csh_printf(csh, "       control raw <0x0000-0x007f>\r\n");
    csh_printf(csh, "       control <charge|power|storage|resistance|launch|takeoff|bms> <on|off>\r\n");
    return -1;
}
CSH_CMD_EXPORT_ALIAS(shell_control, control, );

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

static int shell_clear(int argc, char **argv)
{
    chry_shell_t *csh = shell_from_argv(argc, argv);

    (void)argv;
    if (argc != 1) {
        csh_printf(csh, "usage: clear\r\n");
        return -1;
    }

    csh_printf(csh, "\033[H\033[J");
    return 0;
}
CSH_CMD_EXPORT_ALIAS(shell_clear, clear, );

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

static int shell_watch(int argc, char **argv)
{
    chry_shell_t *csh = shell_from_argv(argc, argv);
    uint32_t interval_ms = WATCH_DEFAULT_INTERVAL_MS;
    uint32_t count = WATCH_DEFAULT_COUNT;

    if (argc > 3) {
        csh_printf(csh, "usage: watch [interval_ms] [count]\r\n");
        csh_printf(csh, "  count 0 means continuous refresh\r\n");
        return -1;
    }

    if ((argc >= 2) && !parse_u32_range(argv[1], 1U, PS_MAX_INTERVAL_MS, &interval_ms)) {
        csh_printf(csh, "invalid interval_ms: %s\r\n", argv[1]);
        csh_printf(csh, "  range: 1-%u\r\n", (unsigned int)PS_MAX_INTERVAL_MS);
        return -1;
    }

    if ((argc == 3) && !parse_u32_range(argv[2], 0U, WATCH_MAX_COUNT, &count)) {
        csh_printf(csh, "invalid count: %s\r\n", argv[2]);
        csh_printf(csh, "  range: 0-%u\r\n", (unsigned int)WATCH_MAX_COUNT);
        return -1;
    }

    for (uint32_t iteration = 0U; (count == 0U) || (iteration < count); iteration++) {
        csh_printf(csh, "\033[H\033[J");
        csh_printf(csh,
                   "watch - %ums refresh  sample:%u\r\n",
                   (unsigned int)interval_ms,
                   (unsigned int)(iteration + 1U));
        print_charger_snapshot(csh, true);
        vTaskDelay(pdMS_TO_TICKS(interval_ms));
    }

    return 0;
}
CSH_CMD_EXPORT_ALIAS(shell_watch, watch, );

static int shell_modbus(int argc, char **argv)
{
    chry_shell_t *csh = shell_from_argv(argc, argv);
    modbus_master_diag_t diag;

    if ((argc == 2) && (strcmp(argv[1], "reset") == 0)) {
        modbus_master_reset_counters();
        csh_printf(csh, "modbus counters reset\r\n");
        return 0;
    }

    if ((argc != 1) && !((argc == 2) && ((strcmp(argv[1], "status") == 0) || (strcmp(argv[1], "counters") == 0)))) {
        csh_printf(csh, "usage: modbus [status|counters|reset]\r\n");
        return -1;
    }

    modbus_master_get_diag(&diag);

    if ((argc == 1) || (strcmp(argv[1], "status") == 0)) {
        csh_printf(csh,
                   "modbus:%s state:%s(%u) tx:%s rx_avail:%u last_len:%d last_err:%d\r\n",
                   diag.initialized ? "ready" : "not_ready",
                   modbus_state_name(diag.state),
                   (unsigned int)diag.state,
                   diag.tx_busy ? "busy" : "idle",
                   (unsigned int)diag.rx_available,
                   diag.last_read_len,
                   diag.last_error_code);
        csh_printf(csh,
                   "control applied:0x%04x pending:0x%04x active:bit%u=%u\r\n",
                   (unsigned int)diag.applied_control_word,
                   (unsigned int)diag.pending_control_mask,
                   (unsigned int)diag.active_control_bit,
                   (unsigned int)(diag.active_control_value ? 1U : 0U));
    }

    if ((argc == 1) || (strcmp(argv[1], "counters") == 0)) {
        csh_printf(csh,
                   "ok control:%u status:%u parameters:%u\r\n",
                   (unsigned int)diag.control_ok_count,
                   (unsigned int)diag.status_ok_count,
                   (unsigned int)diag.parameter_ok_count);
        csh_printf(csh,
                   "err send:%u tx_timeout:%u response_timeout:%u parse:%u\r\n",
                   (unsigned int)diag.send_fail_count,
                   (unsigned int)diag.tx_timeout_count,
                   (unsigned int)diag.response_timeout_count,
                   (unsigned int)diag.parse_fail_count);
    }

    return 0;
}
CSH_CMD_EXPORT_ALIAS(shell_modbus, modbus, );
