/* SPDX-License-Identifier: MIT */
/*
 * Fault management (FM) — alarm lifecycle, shaped after o-ran-fm.
 *
 * The O-RU reports faults to the SMO as alarms with a fault-id, severity,
 * and a raised/cleared state. An alarm is a *stateful* condition (unlike a
 * one-shot log line): it is raised when a fault appears and cleared when it
 * goes away, and re-raising an already-active alarm just refreshes it rather
 * than duplicating it. This module keeps the active-alarm table and a small
 * event history, and can drive alarms automatically from PM counters via
 * threshold checks.
 *
 * It replaces the previous mplane_raise_alarm() log stub with a real,
 * testable alarm store; mplane forwards to it.
 */
#ifndef ORU_FM_H
#define ORU_FM_H

#include "oru/types.h"
#include "oru/perf.h"

#define FM_MAX_ALARMS    32u
#define FM_MAX_HISTORY   64u
#define FM_FAULT_ID_LEN  32u
#define FM_TEXT_LEN      80u

typedef enum {
    FM_CLEARED = 0,   /* not an active condition (used in history)     */
    FM_WARNING,
    FM_MINOR,
    FM_MAJOR,
    FM_CRITICAL,
} fm_severity_t;

/* One active alarm. */
typedef struct {
    char          fault_id[FM_FAULT_ID_LEN];
    char          text[FM_TEXT_LEN];
    fm_severity_t severity;
    uint64_t      raise_count;   /* how many times raised while active   */
    bool          active;
} fm_alarm_t;

/* One history event (raise or clear). */
typedef struct {
    char          fault_id[FM_FAULT_ID_LEN];
    fm_severity_t severity;      /* FM_CLEARED marks a clear event       */
    uint64_t      seq;           /* monotonically increasing event id    */
} fm_event_t;

typedef struct {
    fm_alarm_t alarms[FM_MAX_ALARMS];
    fm_event_t history[FM_MAX_HISTORY];
    size_t     hist_head;        /* ring buffer write index              */
    size_t     hist_count;       /* total events written (may exceed cap)*/
    uint64_t   event_seq;        /* next event id                        */
} fm_t;

void fm_init(fm_t *f);

/*
 * Raise (or refresh) an alarm. If `fault_id` is already active its
 * raise_count is bumped and severity/text updated; otherwise a new alarm is
 * allocated. Returns ORU_ERR if the table is full. Logs a history event
 * only on the initial raise (transition into active).
 */
oru_status_t fm_raise(fm_t *f, const char *fault_id, fm_severity_t sev,
                      const char *text);

/* Clear an active alarm. Returns ORU_OK if it was active (and logs a clear
 * event), ORU_ERR_PARAM if no such active alarm. */
oru_status_t fm_clear(fm_t *f, const char *fault_id);

/* Is this fault currently active? */
bool fm_is_active(const fm_t *f, const char *fault_id);

/* Number of currently active alarms. */
size_t fm_active_count(const fm_t *f);

/* Highest severity among active alarms (FM_CLEARED if none active). */
fm_severity_t fm_max_severity(const fm_t *f);

/* Thresholds for the automatic PM->alarm checks. */
typedef struct {
    uint64_t dl_late_max;      /* raise if dl_late exceeds this          */
    uint64_t ul_late_max;
    uint64_t seq_gaps_max;
    uint64_t cu_orphan_max;
    double   evm_pct_max;      /* raise if mean EVM% exceeds this        */
} fm_thresholds_t;

/*
 * Evaluate a completed PM interval (p->last) against `th`, raising or
 * clearing the corresponding alarms. Returns the number of alarms currently
 * active after evaluation.
 */
size_t fm_check_perf(fm_t *f, const perf_t *p, const fm_thresholds_t *th);

const char *fm_severity_str(fm_severity_t s);

/* Serialise the active-alarm list as o-ran-fm-shaped JSON. Returns bytes
 * written (excl. NUL) or a negative oru_status_t. */
int fm_to_json(const fm_t *f, char *buf, size_t buf_len);

#endif /* ORU_FM_H */
