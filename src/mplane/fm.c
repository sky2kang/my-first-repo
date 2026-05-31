/* SPDX-License-Identifier: MIT */
#include "oru/fm.h"
#include "oru/log.h"

#include <stdio.h>
#include <string.h>

#define TAG "fm"

const char *fm_severity_str(fm_severity_t s)
{
    switch (s) {
    case FM_CLEARED:  return "CLEARED";
    case FM_WARNING:  return "WARNING";
    case FM_MINOR:    return "MINOR";
    case FM_MAJOR:    return "MAJOR";
    case FM_CRITICAL: return "CRITICAL";
    default:          return "?";
    }
}

void fm_init(fm_t *f)
{
    if (f)
        memset(f, 0, sizeof(*f));
}

static fm_alarm_t *find_active(fm_t *f, const char *fault_id)
{
    for (size_t i = 0; i < FM_MAX_ALARMS; i++)
        if (f->alarms[i].active &&
            strcmp(f->alarms[i].fault_id, fault_id) == 0)
            return &f->alarms[i];
    return NULL;
}

static fm_alarm_t *find_free(fm_t *f)
{
    for (size_t i = 0; i < FM_MAX_ALARMS; i++)
        if (!f->alarms[i].active)
            return &f->alarms[i];
    return NULL;
}

static void push_event(fm_t *f, const char *fault_id, fm_severity_t sev)
{
    fm_event_t *e = &f->history[f->hist_head];
    snprintf(e->fault_id, sizeof(e->fault_id), "%s", fault_id);
    e->severity = sev;
    e->seq = f->event_seq++;
    f->hist_head = (f->hist_head + 1) % FM_MAX_HISTORY;
    f->hist_count++;
}

oru_status_t fm_raise(fm_t *f, const char *fault_id, fm_severity_t sev,
                      const char *text)
{
    if (!f || !fault_id || sev == FM_CLEARED)
        return ORU_ERR_PARAM;

    fm_alarm_t *a = find_active(f, fault_id);
    if (a) {
        /* refresh an already-active alarm (no new history event) */
        a->raise_count++;
        a->severity = sev;
        if (text)
            snprintf(a->text, sizeof(a->text), "%s", text);
        return ORU_OK;
    }

    a = find_free(f);
    if (!a) {
        LOGE(TAG, "alarm table full, dropping %s", fault_id);
        return ORU_ERR;
    }
    snprintf(a->fault_id, sizeof(a->fault_id), "%s", fault_id);
    snprintf(a->text, sizeof(a->text), "%s", text ? text : "");
    a->severity = sev;
    a->raise_count = 1;
    a->active = true;
    push_event(f, fault_id, sev);
    LOGW(TAG, "ALARM RAISED [%s] %s: %s", fm_severity_str(sev), fault_id,
         text ? text : "");
    return ORU_OK;
}

oru_status_t fm_clear(fm_t *f, const char *fault_id)
{
    if (!f || !fault_id)
        return ORU_ERR_PARAM;

    fm_alarm_t *a = find_active(f, fault_id);
    if (!a)
        return ORU_ERR_PARAM;

    a->active = false;
    push_event(f, fault_id, FM_CLEARED);
    LOGI(TAG, "ALARM CLEARED %s", fault_id);
    return ORU_OK;
}

bool fm_is_active(const fm_t *f, const char *fault_id)
{
    if (!f || !fault_id)
        return false;
    for (size_t i = 0; i < FM_MAX_ALARMS; i++)
        if (f->alarms[i].active &&
            strcmp(f->alarms[i].fault_id, fault_id) == 0)
            return true;
    return false;
}

size_t fm_active_count(const fm_t *f)
{
    if (!f)
        return 0;
    size_t n = 0;
    for (size_t i = 0; i < FM_MAX_ALARMS; i++)
        if (f->alarms[i].active)
            n++;
    return n;
}

fm_severity_t fm_max_severity(const fm_t *f)
{
    fm_severity_t max = FM_CLEARED;
    if (!f)
        return max;
    for (size_t i = 0; i < FM_MAX_ALARMS; i++)
        if (f->alarms[i].active && f->alarms[i].severity > max)
            max = f->alarms[i].severity;
    return max;
}

/* Raise sev if `over` is true, otherwise clear the fault if it was active. */
static void check_one(fm_t *f, bool over, const char *fault_id,
                      fm_severity_t sev, const char *text)
{
    if (over)
        fm_raise(f, fault_id, sev, text);
    else if (fm_is_active(f, fault_id))
        fm_clear(f, fault_id);
}

size_t fm_check_perf(fm_t *f, const perf_t *p, const fm_thresholds_t *th)
{
    if (!f || !p || !th)
        return 0;

    const perf_counters_t *c = &p->last;
    double mean_evm =
        (c->evm_samples ? (double)c->evm_pct_x100_sum
                          / (double)c->evm_samples / 100.0
                        : 0.0);

    check_one(f, c->dl_late  > th->dl_late_max,  "DL_LATE_HIGH",
              FM_MAJOR, "DL packets missing the slot window");
    check_one(f, c->ul_late  > th->ul_late_max,  "UL_LATE_HIGH",
              FM_MINOR, "UL packets past the Ta3 deadline");
    check_one(f, c->seq_gaps > th->seq_gaps_max, "FH_SEQ_GAPS",
              FM_MINOR, "fronthaul sequence discontinuities");
    check_one(f, c->cu_orphan > th->cu_orphan_max, "CU_ORPHAN",
              FM_WARNING, "U-plane with no matching C-plane grant");
    check_one(f, mean_evm > th->evm_pct_max, "EVM_HIGH",
              FM_MAJOR, "transmit EVM above limit");

    return fm_active_count(f);
}

int fm_to_json(const fm_t *f, char *buf, size_t buf_len)
{
    if (!f || !buf)
        return ORU_ERR_PARAM;

    size_t off = 0;
    int n = snprintf(buf, buf_len,
        "{\n  \"o-ran-fm:active-alarm-list\": {\n    \"active-alarms\": [");
    if (n < 0)
        return ORU_ERR;
    off = (size_t)n;

    bool first = true;
    for (size_t i = 0; i < FM_MAX_ALARMS; i++) {
        const fm_alarm_t *a = &f->alarms[i];
        if (!a->active)
            continue;
        n = snprintf(buf + off, (off < buf_len) ? buf_len - off : 0,
            "%s\n      {\n"
            "        \"fault-id\": \"%s\",\n"
            "        \"fault-severity\": \"%s\",\n"
            "        \"is-cleared\": false,\n"
            "        \"raise-count\": %llu,\n"
            "        \"fault-text\": \"%s\"\n"
            "      }",
            first ? "" : ",",
            a->fault_id, fm_severity_str(a->severity),
            (unsigned long long)a->raise_count, a->text);
        if (n < 0)
            return ORU_ERR;
        off += (size_t)n;
        first = false;
    }

    n = snprintf(buf + off, (off < buf_len) ? buf_len - off : 0,
                 "\n    ]\n  }\n}\n");
    if (n < 0)
        return ORU_ERR;
    off += (size_t)n;

    if (off >= buf_len) {
        LOGE(TAG, "FM JSON buffer too small (need %zu, have %zu)",
             off + 1, buf_len);
        return ORU_ERR_PARAM;
    }
    return (int)off;
}
