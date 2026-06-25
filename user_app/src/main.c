#include <stdio.h>

#include "FreeRTOS.h"
#include "app_log.h"
#include "board.h"
#include "charger_app.h"
#include "ecat_slave.h"
#include "hpm_clock_drv.h"
#include "hpm_ota.h"
#include "hpm_uart_drv.h"
#include "modbus_master_app.h"
#include "oled.h"
#include "shell.h"
#include "task.h"

#define ECAT_TASK_PRIORITY       (configMAX_PRIORITIES - 2)
#define SHELL_TASK_PRIORITY      (configMAX_PRIORITIES - 4)
#define MODBUS_TASK_PRIORITY     (configMAX_PRIORITIES - 5)
#define STATUS_TASK_PRIORITY     (configMAX_PRIORITIES - 6)
#define OLED_TASK_PRIORITY       (configMAX_PRIORITIES - 7)

SDK_DECLARE_EXT_ISR_M(BOARD_CONSOLE_UART_IRQ, shell_uart_isr)

static void ecat_task(void *pvParameters)
{
    (void)pvParameters;

    if (ecat_slave_init() != 0) {
        printf("EtherCAT slave init failed.\r\n");
        vTaskDelete(NULL);
    }

    while (1) {
        ecat_slave_poll();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

static void shell_task(void *pvParameters)
{
    uart_config_t shell_uart_config = { 0 };

    (void)pvParameters;

    uart_default_config(BOARD_CONSOLE_UART_BASE, &shell_uart_config);
    shell_uart_config.src_freq_in_hz = clock_get_frequency(BOARD_CONSOLE_UART_CLK_NAME);
    shell_uart_config.baudrate = BOARD_CONSOLE_UART_BAUDRATE;

    if (status_success != uart_init(BOARD_CONSOLE_UART_BASE, &shell_uart_config)) {
        printf("Shell uart init failed.\r\n");
        vTaskDelete(NULL);
    }

    if (shell_init(BOARD_CONSOLE_UART_BASE, false) != 0) {
        printf("Shell init failed.\r\n");
        vTaskDelete(NULL);
    }

    uart_enable_irq(BOARD_CONSOLE_UART_BASE, uart_intr_rx_data_avail_or_timeout);
    intc_m_enable_irq_with_priority(BOARD_CONSOLE_UART_IRQ, 1);

    app_log_set_shell_ready(true);
    app_log_printf("Shell ready on UART0 115200 8N1.\r\n");

    vTaskDelete(NULL);
}

static void status_task(void *pvParameters)
{
    TickType_t last_wake = xTaskGetTickCount();
    charger_txpdo_t txpdo;

    (void)pvParameters;

    while (1) {
        charger_app_get_txpdo(&txpdo);
        board_led_toggle();
        app_log_printf("status:0x%04x soc:%u sys:%umV bat:%umV charge:%umA discharge:%umA resistance:%umOhm ota_app:%d\r\n",
                       txpdo.status_word,
                       txpdo.battery_level_x100,
                       txpdo.sys_input_voltage_mv,
                       txpdo.battery_voltage_mv,
                       txpdo.charge_current_ma,
                       txpdo.discharge_current_ma,
                       txpdo.internal_resistance_mohm,
                       hpm_ota_get_nowrunning_app());
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
    }
}

static void oled_show_line(uint8_t y, const char *text)
{
    OLED_ShowString(0, y, (uint8_t *)text, 8, 1);
}

static void oled_task(void *pvParameters)
{
    charger_txpdo_t txpdo;
    char line[24];

    (void)pvParameters;

    OLED_Init();
    OLED_ClearBuffer();
    oled_show_line(0, "EtherCAT Charger");
    OLED_Refresh();

    while (1) {
        charger_app_get_txpdo(&txpdo);

        OLED_ClearBuffer();
        oled_show_line(0, "EtherCAT Charger");

        snprintf(line, sizeof(line), "S:%04X SOC:%u",
                 (unsigned int)txpdo.status_word,
                 (unsigned int)txpdo.battery_level_x100);
        oled_show_line(16, line);

        snprintf(line, sizeof(line), "SYS:%u BAT:%u",
                 (unsigned int)txpdo.sys_input_voltage_mv,
                 (unsigned int)txpdo.battery_voltage_mv);
        oled_show_line(32, line);

        snprintf(line, sizeof(line), "C:%u D:%u R:%u",
                 (unsigned int)txpdo.charge_current_ma,
                 (unsigned int)txpdo.discharge_current_ma,
                 (unsigned int)txpdo.internal_resistance_mohm);
        oled_show_line(48, line);

        OLED_Refresh();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

int main(void)
{
    board_init();
    board_init_led_pins();
    charger_app_init();

    printf("EtherCAT charger user app, OTA%d\r\n", hpm_ota_get_nowrunning_app());

    xTaskCreate(shell_task,
                "shell",
                configMINIMAL_STACK_SIZE * 2,
                NULL,
                SHELL_TASK_PRIORITY,
                NULL);
    xTaskCreate(ecat_task,
                "ecat",
                configMINIMAL_STACK_SIZE * 2,
                NULL,
                ECAT_TASK_PRIORITY,
                NULL);
    xTaskCreate(modbus_master_task,
                "modbus",
                configMINIMAL_STACK_SIZE * 3,
                NULL,
                MODBUS_TASK_PRIORITY,
                NULL);
    xTaskCreate(status_task,
                "status",
                configMINIMAL_STACK_SIZE,
                NULL,
                STATUS_TASK_PRIORITY,
                NULL);
    xTaskCreate(oled_task,
                "oled",
                configMINIMAL_STACK_SIZE * 2,
                NULL,
                OLED_TASK_PRIORITY,
                NULL);

    vTaskStartScheduler();

    while (1) {
    }
}
