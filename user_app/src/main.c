#include <stdio.h>

#include "FreeRTOS.h"
#include "board.h"
#include "charger_app.h"
#include "hpm_ota.h"
#include "ota_port.h"
#include "task.h"

#define ECAT_OTA_TASK_PRIORITY   (configMAX_PRIORITIES - 2)
#define CHARGER_TASK_PRIORITY    (configMAX_PRIORITIES - 4)
#define STATUS_TASK_PRIORITY     (configMAX_PRIORITIES - 6)

static charger_rxpdo_t s_demo_rxpdo;

static void ecat_ota_task(void *pvParameters)
{
    (void)pvParameters;

    if (hpm_ota_init() != 0) {
        printf("FoE OTA init failed.\r\n");
        vTaskDelete(NULL);
    }

    while (1) {
        hpm_ota_polling_handle();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

static void charger_task(void *pvParameters)
{
    (void)pvParameters;

    charger_app_init();
    s_demo_rxpdo.target_voltage_mv = 54000U;
    s_demo_rxpdo.target_current_ma = 10000U;

    while (1) {
        charger_app_set_rxpdo(&s_demo_rxpdo);
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

int main(void)
{
    board_init();
    board_init_led_pins();

    printf("EtherCAT charger user app, OTA%d\r\n", hpm_ota_get_nowrunning_app());

    xTaskCreate(ecat_ota_task,
                "ecat_ota",
                configMINIMAL_STACK_SIZE * 2,
                NULL,
                ECAT_OTA_TASK_PRIORITY,
                NULL);
    xTaskCreate(charger_task,
                "charger",
                configMINIMAL_STACK_SIZE,
                NULL,
                CHARGER_TASK_PRIORITY,
                NULL);
    xTaskCreate(status_task,
                "status",
                configMINIMAL_STACK_SIZE,
                NULL,
                STATUS_TASK_PRIORITY,
                NULL);

    vTaskStartScheduler();

    while (1) {
    }
}

