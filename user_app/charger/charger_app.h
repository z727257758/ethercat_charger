#ifndef ETHERCAT_CHARGER_APP_H
#define ETHERCAT_CHARGER_APP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t control_word;
} charger_rxpdo_t;

typedef struct {
    uint16_t status_word;
    uint16_t battery_level_x100;
    uint16_t sys_input_voltage_mv;
    uint16_t battery_voltage_mv;
    uint16_t charge_current_ma;
    uint16_t discharge_current_ma;
    uint16_t internal_resistance_mohm;
} charger_txpdo_t;

_Static_assert(sizeof(charger_rxpdo_t) == 2U, "RxPDO must be 2 bytes");
_Static_assert(sizeof(charger_txpdo_t) == 14U, "TxPDO must be 14 bytes");

void charger_app_init(void);
void charger_app_set_rxpdo(const charger_rxpdo_t *rxpdo);
void charger_app_get_rxpdo(charger_rxpdo_t *rxpdo);
void charger_app_set_modbus_feedback(const charger_txpdo_t *txpdo);
void charger_app_get_txpdo(charger_txpdo_t *txpdo);

#ifdef __cplusplus
}
#endif

#endif
