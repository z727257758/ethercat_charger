#include "charger_app.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdbool.h>
#include <string.h>

#define CHARGER_STATUS_READY      (1U << 0)
#define CHARGER_STATUS_ENABLED    (1U << 1)
#define CHARGER_STATUS_FAULT      (1U << 2)

static charger_rxpdo_t s_rxpdo;
static charger_txpdo_t s_txpdo;
static charger_state_t s_state;
static bool s_modbus_feedback_valid;

static void charger_update_status_word(void)
{
    uint16_t status = 0;

    if (s_state == charger_state_ready || s_state == charger_state_charging) {
        status |= CHARGER_STATUS_READY;
    }
    if (s_state == charger_state_charging) {
        status |= CHARGER_STATUS_ENABLED;
    }
    if (s_state == charger_state_fault) {
        status |= CHARGER_STATUS_FAULT;
    }

    s_txpdo.status_word = status;
}

void charger_app_init(void)
{
    taskENTER_CRITICAL();
    memset(&s_rxpdo, 0, sizeof(s_rxpdo));
    memset(&s_txpdo, 0, sizeof(s_txpdo));
    s_state = charger_state_idle;
    s_modbus_feedback_valid = false;
    charger_update_status_word();
    taskEXIT_CRITICAL();
}

void charger_app_step(void)
{
    charger_rxpdo_t rxpdo;
    bool feedback_valid;

    taskENTER_CRITICAL();
    rxpdo = s_rxpdo;
    feedback_valid = s_modbus_feedback_valid;
    taskEXIT_CRITICAL();

    taskENTER_CRITICAL();
    switch ((charger_command_t)rxpdo.command) {
    case charger_command_enable:
        if (rxpdo.target_voltage_mv > 0U && rxpdo.target_current_ma > 0U) {
            s_state = charger_state_charging;
        } else {
            s_state = charger_state_fault;
            s_txpdo.fault_code = 1U;
        }
        break;
    case charger_command_disable:
        s_state = charger_state_ready;
        break;
    case charger_command_clear_fault:
        s_txpdo.fault_code = 0U;
        s_state = charger_state_idle;
        break;
    case charger_command_none:
    default:
        if (s_state == charger_state_idle) {
            s_state = charger_state_ready;
        }
        break;
    }

    if (!feedback_valid) {
        if (s_state == charger_state_charging) {
            s_txpdo.measured_voltage_mv = rxpdo.target_voltage_mv;
            s_txpdo.measured_current_ma = rxpdo.target_current_ma;
        } else {
            s_txpdo.measured_current_ma = 0U;
        }
        charger_update_status_word();
    }
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
        s_modbus_feedback_valid = true;
        if ((s_txpdo.status_word & CHARGER_STATUS_FAULT) != 0U) {
            s_state = charger_state_fault;
        } else if ((s_txpdo.status_word & CHARGER_STATUS_ENABLED) != 0U) {
            s_state = charger_state_charging;
        } else if ((s_txpdo.status_word & CHARGER_STATUS_READY) != 0U) {
            s_state = charger_state_ready;
        } else {
            s_state = charger_state_idle;
        }
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

charger_state_t charger_app_get_state(void)
{
    charger_state_t state;

    taskENTER_CRITICAL();
    state = s_state;
    taskEXIT_CRITICAL();

    return state;
}
