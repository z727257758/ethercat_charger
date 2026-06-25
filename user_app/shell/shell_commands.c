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
