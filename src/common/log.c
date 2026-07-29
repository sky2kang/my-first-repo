/* SPDX-License-Identifier: MIT */
#include "oru/log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static oru_log_level_t g_level = ORU_LOG_INFO;

static const char *level_str(oru_log_level_t l)
{
    switch (l) {
    case ORU_LOG_TRACE: return "TRACE";
    case ORU_LOG_DEBUG: return "DEBUG";
    case ORU_LOG_INFO:  return "INFO ";
    case ORU_LOG_WARN:  return "WARN ";
    case ORU_LOG_ERROR: return "ERROR";
    default:            return "?????";
    }
}

void oru_log_set_level(oru_log_level_t level)
{
    g_level = level;
}

void oru_log(oru_log_level_t level, const char *tag, const char *fmt, ...)
{
    if (level < g_level)
        return;

    char ts[16] = {0};
    time_t now = time(NULL);
    struct tm tmv;
#if defined(_WIN32)
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    strftime(ts, sizeof(ts), "%H:%M:%S", &tmv);

    fprintf(stderr, "%s [%s] %-9s ", ts, level_str(level), tag ? tag : "-");

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fputc('\n', stderr);
}
