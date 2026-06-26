#ifndef MODBUS_MASTER_APP_H
#define MODBUS_MASTER_APP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool initialized;
    bool tx_busy;
    uint32_t state;
    uint32_t rx_available;
    int last_read_len;
    int last_error_code;
    uint16_t applied_control_word;
    uint16_t pending_control_mask;
    uint8_t active_control_bit;
    bool active_control_value;
    uint32_t control_ok_count;
    uint32_t status_ok_count;
    uint32_t parameter_ok_count;
    uint32_t send_fail_count;
    uint32_t tx_timeout_count;
    uint32_t response_timeout_count;
    uint32_t parse_fail_count;
} modbus_master_diag_t;

void modbus_master_init(void);
void modbus_master_task(void *pvParameters);
void modbus_master_get_diag(modbus_master_diag_t *diag);
void modbus_master_reset_counters(void);

#ifdef __cplusplus
}
#endif

#endif
