#include "charger_app.h"

#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

static charger_rxpdo_t s_rxpdo;
static charger_txpdo_t s_txpdo;

void charger_app_init(void)
{
    taskENTER_CRITICAL();
    memset(&s_rxpdo, 0, sizeof(s_rxpdo));
    memset(&s_txpdo, 0, sizeof(s_txpdo));
    taskEXIT_CRITICAL();
}

void charger_app_set_rxpdo(const charger_rxpdo_t *rxpdo)
{
    if (rxpdo != NULL) {
        taskENTER_CRITICAL();
        s_rxpdo = *rxpdo;
        taskEXIT_CRITICAL();
    }
}

void charger_app_get_rxpdo(charger_rxpdo_t *rxpdo)
{
    if (rxpdo != NULL) {
        taskENTER_CRITICAL();
        *rxpdo = s_rxpdo;
        taskEXIT_CRITICAL();
    }
}

void charger_app_set_modbus_feedback(const charger_txpdo_t *txpdo)
{
    if (txpdo != NULL) {
        taskENTER_CRITICAL();
        s_txpdo = *txpdo;
        taskEXIT_CRITICAL();
    }
}

void charger_app_get_txpdo(charger_txpdo_t *txpdo)
{
    if (txpdo != NULL) {
        taskENTER_CRITICAL();
        *txpdo = s_txpdo;
        taskEXIT_CRITICAL();
    }
}
