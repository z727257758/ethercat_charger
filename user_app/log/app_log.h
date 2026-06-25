#ifndef APP_LOG_H
#define APP_LOG_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void app_log_set_enabled(bool enabled);
bool app_log_is_enabled(void);
void app_log_set_shell_ready(bool ready);
int app_log_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
