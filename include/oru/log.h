/* SPDX-License-Identifier: MIT */
/* Tiny leveled logger with per-module tags. */
#ifndef ORU_LOG_H
#define ORU_LOG_H

typedef enum {
    ORU_LOG_TRACE = 0,
    ORU_LOG_DEBUG,
    ORU_LOG_INFO,
    ORU_LOG_WARN,
    ORU_LOG_ERROR,
} oru_log_level_t;

void oru_log_set_level(oru_log_level_t level);
void oru_log(oru_log_level_t level, const char *tag, const char *fmt, ...);

#define LOGT(tag, ...) oru_log(ORU_LOG_TRACE, tag, __VA_ARGS__)
#define LOGD(tag, ...) oru_log(ORU_LOG_DEBUG, tag, __VA_ARGS__)
#define LOGI(tag, ...) oru_log(ORU_LOG_INFO,  tag, __VA_ARGS__)
#define LOGW(tag, ...) oru_log(ORU_LOG_WARN,  tag, __VA_ARGS__)
#define LOGE(tag, ...) oru_log(ORU_LOG_ERROR, tag, __VA_ARGS__)

#endif /* ORU_LOG_H */
