#ifndef ETHERCAT_CHARGER_ECAT_SLAVE_H
#define ETHERCAT_CHARGER_ECAT_SLAVE_H

#ifdef __cplusplus
extern "C" {
#endif

int ecat_slave_init(void);
void ecat_slave_poll(void);

#ifdef __cplusplus
}
#endif

#endif
