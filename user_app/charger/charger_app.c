#include "charger_app.h"

#include <string.h>

#define CHARGER_STATUS_READY      (1U << 0)
#define CHARGER_STATUS_ENABLED    (1U << 1)
#define CHARGER_STATUS_FAULT      (1U << 2)

static charger_rxpdo_t s_rxpdo;
static charger_txpdo_t s_txpdo;
static charger_state_t s_state;

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
    memset(&s_rxpdo, 0, sizeof(s_rxpdo));
    memset(&s_txpdo, 0, sizeof(s_txpdo));
    s_state = charger_state_idle;
    charger_update_status_word();
}

void charger_app_step(void)
{
    switch ((charger_command_t)s_rxpdo.command) {
    case charger_command_enable:
        if (s_rxpdo.target_voltage_mv > 0U && s_rxpdo.target_current_ma > 0U) {
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

    if (s_state == charger_state_charging) {
        s_txpdo.measured_voltage_mv = s_rxpdo.target_voltage_mv;
        s_txpdo.measured_current_ma = s_rxpdo.target_current_ma;
    } else {
        s_txpdo.measured_current_ma = 0U;
    }

    charger_update_status_word();
}

void charger_app_set_rxpdo(const charger_rxpdo_t *rxpdo)
{
    if (rxpdo != NULL) {
        s_rxpdo = *rxpdo;
    }
}

void charger_app_get_txpdo(charger_txpdo_t *txpdo)
{
    if (txpdo != NULL) {
        *txpdo = s_txpdo;
    }
}

charger_state_t charger_app_get_state(void)
{
    return s_state;
}

