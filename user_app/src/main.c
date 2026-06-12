#include <stdio.h>

#include "FreeRTOS.h"
#include "board.h"
#include "charger_app.h"
#include "ecat_slave.h"
#include "hpm_ota.h"
#include "modbus_master_app.h"
#include "oled.h"
#include "task.h"

#define ECAT_TASK_PRIORITY       (configMAX_PRIORITIES - 2)
#define CHARGER_TASK_PRIORITY    (configMAX_PRIORITIES - 4)
#define MODBUS_TASK_PRIORITY     (configMAX_PRIORITIES - 5)
#define STATUS_TASK_PRIORITY     (configMAX_PRIORITIES - 6)
#define OLED_TASK_PRIORITY       (configMAX_PRIORITIES - 7)

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

static void charger_task(void *pvParameters)
{
    (void)pvParameters;

    charger_app_init();

    while (1) {
        charger_app_step();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void status_task(void *pvParameters)
{
    TickType_t last_wake = xTaskGetTickCount();
    charger_txpdo_t txpdo;

    (void)pvParameters;

    while (1) {
        charger_app_get_txpdo(&txpdo);
        board_led_toggle();
        printf("charger state:%d status:0x%04x fault:%u ota_app:%d\r\n",
               charger_app_get_state(),
               txpdo.status_word,
               txpdo.fault_code,
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
    OLED_Clear();
    oled_show_line(0, "EtherCAT Charger");
    OLED_Refresh();

    while (1) {
        charger_app_get_txpdo(&txpdo);

        OLED_Clear();
        oled_show_line(0, "EtherCAT Charger");

        snprintf(line, sizeof(line), "State:%u OTA:%d",
                 (unsigned int)charger_app_get_state(),
                 hpm_ota_get_nowrunning_app());
        oled_show_line(16, line);

        snprintf(line, sizeof(line), "V:%lumV",
                 (unsigned long)txpdo.measured_voltage_mv);
        oled_show_line(32, line);

        snprintf(line, sizeof(line), "I:%lumA F:%u",
                 (unsigned long)txpdo.measured_current_ma,
                 (unsigned int)txpdo.fault_code);
        oled_show_line(48, line);

        OLED_Refresh();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

int main(void)
{
    board_init();
    board_init_led_pins();

    printf("EtherCAT charger user app, OTA%d\r\n", hpm_ota_get_nowrunning_app());

    xTaskCreate(ecat_task,
                "ecat",
                configMINIMAL_STACK_SIZE * 2,
                NULL,
                ECAT_TASK_PRIORITY,
                NULL);
    xTaskCreate(charger_task,
                "charger",
                configMINIMAL_STACK_SIZE,
                NULL,
                CHARGER_TASK_PRIORITY,
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
