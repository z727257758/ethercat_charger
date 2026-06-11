#ifndef ETHERCAT_CHARGER_OTA_CONFIG_H
#define ETHERCAT_CHARGER_OTA_CONFIG_H

#include <stdlib.h>
#include "hpm_soc.h"

#if defined(CONFIG_FREERTOS) && CONFIG_FREERTOS
#include "FreeRTOS.h"
#include "semphr.h"

typedef SemaphoreHandle_t hpm_mutex_handle_t;

#define HPM_WAIT_FOREVER portMAX_DELAY

static inline hpm_mutex_handle_t hpm_mutex_create(const char *name)
{
    (void)name;
    return xSemaphoreCreateMutex();
}

static inline void hpm_mutex_get(hpm_mutex_handle_t mutex, TickType_t timeout)
{
    if (mutex != NULL) {
        (void)xSemaphoreTake(mutex, timeout);
    }
}

static inline void hpm_mutex_put(hpm_mutex_handle_t mutex)
{
    if (mutex != NULL) {
        (void)xSemaphoreGive(mutex);
    }
}
#endif

#define HEADER_INIT_VERSION         (0x00010000U)

#define ota_malloc(...)             malloc(__VA_ARGS__)
#define ota_free(...)               free(__VA_ARGS__)

#ifdef HPM_PGPR0
#define HPM_OTA_INFO_RAM_ADDR       (HPM_PGPR0)
#else
#define HPM_OTA_INFO_RAM_ADDR       (HPM_PGPR)
#endif

#define HPM_OTA_USER_EXIP_INDEX     (3)
#define HPM_OTA_RETRY_COUNT         (3)

#define CONFIG_HPM_PRINTF(...)      printf(__VA_ARGS__)
#define CONFIG_HPM_DBG_LEVEL        HPM_DBG_INFO

#endif
