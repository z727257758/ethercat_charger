#ifndef MODBUS_MASTER_APP_H
#define MODBUS_MASTER_APP_H

#ifdef __cplusplus
extern "C" {
#endif

void modbus_master_init(void);
void modbus_master_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif
