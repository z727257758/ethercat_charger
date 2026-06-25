#include "app_log.h"

#include <stdarg.h>
#include <stdio.h>

#include "shell.h"

static volatile bool s_app_log_enabled = true;
static volatile bool s_shell_ready;

void app_log_set_enabled(bool enabled)
{
    s_app_log_enabled = enabled;
}

bool app_log_is_enabled(void)
{
    return s_app_log_enabled;
}

void app_log_set_shell_ready(bool ready)
{
    s_shell_ready = ready;
}

int app_log_printf(const char *fmt, ...)
{
    int ret;
    va_list args;

    if (!s_app_log_enabled) {
        return 0;
    }

    if (s_shell_ready) {
        shell_lock();
    }

    va_start(args, fmt);
    ret = vprintf(fmt, args);
    va_end(args);

    if (s_shell_ready) {
        shell_unlock();
    }

    return ret;
}
