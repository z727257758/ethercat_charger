#ifndef ETHERCAT_CHARGER_APP_H
#define ETHERCAT_CHARGER_APP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    charger_state_idle = 0,
    charger_state_ready,
    charger_state_charging,
    charger_state_fault,
} charger_state_t;

typedef enum {
    charger_command_none = 0,
    charger_command_enable = 1,
    charger_command_disable = 2,
    charger_command_clear_fault = 3,
} charger_command_t;

typedef struct {
    uint16_t control_word;
    uint32_t target_voltage_mv;
    uint32_t target_current_ma;
    uint16_t command;
} charger_rxpdo_t;

typedef struct {
    uint16_t status_word;
    uint32_t measured_voltage_mv;
    uint32_t measured_current_ma;
    uint16_t fault_code;
    uint16_t ota_state;
} charger_txpdo_t;

void charger_app_init(void);
void charger_app_step(void);
void charger_app_set_rxpdo(const charger_rxpdo_t *rxpdo);
void charger_app_get_rxpdo(charger_rxpdo_t *rxpdo);
void charger_app_set_modbus_feedback(const charger_txpdo_t *txpdo);
void charger_app_get_txpdo(charger_txpdo_t *txpdo);
charger_state_t charger_app_get_state(void);

#ifdef __cplusplus
}
#endif

#endif
