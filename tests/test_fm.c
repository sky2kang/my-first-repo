/* SPDX-License-Identifier: MIT */
/* Tests for fault management (oru/fm.h). */
#include "oru/fm.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_raise_clear_lifecycle(void)
{
    fm_t f;
    fm_init(&f);

    assert(fm_active_count(&f) == 0);
    assert(fm_raise(&f, "JESD_FAIL", FM_CRITICAL, "no lock") == ORU_OK);
    assert(fm_is_active(&f, "JESD_FAIL"));
    assert(fm_active_count(&f) == 1);
    assert(fm_max_severity(&f) == FM_CRITICAL);

    /* clearing makes it inactive */
    assert(fm_clear(&f, "JESD_FAIL") == ORU_OK);
    assert(!fm_is_active(&f, "JESD_FAIL"));
    assert(fm_active_count(&f) == 0);
    assert(fm_max_severity(&f) == FM_CLEARED);

    /* clearing again is an error (nothing active) */
    assert(fm_clear(&f, "JESD_FAIL") == ORU_ERR_PARAM);
}

static void test_reraise_refreshes(void)
{
    fm_t f;
    fm_init(&f);

    fm_raise(&f, "PTP_FAIL", FM_MAJOR, "unlocked");
    fm_raise(&f, "PTP_FAIL", FM_MAJOR, "still unlocked");
    fm_raise(&f, "PTP_FAIL", FM_MAJOR, "yet again");

    /* still just one active alarm, raise_count tracks repeats */
    assert(fm_active_count(&f) == 1);
    /* exactly one raise history event (the initial transition) + the
     * absence of clears: history count == 1 */
    assert(f.hist_count == 1);
}

static void test_max_severity(void)
{
    fm_t f;
    fm_init(&f);
    fm_raise(&f, "A", FM_WARNING, "");
    fm_raise(&f, "B", FM_CRITICAL, "");
    fm_raise(&f, "C", FM_MINOR, "");
    assert(fm_max_severity(&f) == FM_CRITICAL);
    fm_clear(&f, "B");
    /* remaining active: A=WARNING, C=MINOR; MINOR outranks WARNING */
    assert(fm_max_severity(&f) == FM_MINOR);
}

static void test_perf_thresholds(void)
{
    fm_t f;
    fm_init(&f);

    fm_thresholds_t th = {
        .dl_late_max = 5, .ul_late_max = 5, .seq_gaps_max = 0,
        .cu_orphan_max = 0, .evm_pct_max = 3.0,
    };

    perf_t p;
    perf_init(&p);
    /* an interval that breaches dl_late and EVM but not the others */
    for (int i = 0; i < 10; i++) perf_dl_window(&p, 0, 0, 1);  /* 10 late */
    perf_evm_sample(&p, 5.0);                                  /* EVM 5% */
    perf_snapshot(&p);

    size_t active = fm_check_perf(&f, &p, &th);
    assert(active == 2);
    assert(fm_is_active(&f, "DL_LATE_HIGH"));
    assert(fm_is_active(&f, "EVM_HIGH"));
    assert(!fm_is_active(&f, "UL_LATE_HIGH"));

    /* next clean interval clears them */
    perf_t good;
    perf_init(&good);
    perf_dl_window(&good, 1, 0, 0);
    perf_evm_sample(&good, 1.0);
    perf_snapshot(&good);
    active = fm_check_perf(&f, &good, &th);
    assert(active == 0);
    assert(!fm_is_active(&f, "DL_LATE_HIGH"));
    assert(!fm_is_active(&f, "EVM_HIGH"));
}

static void test_json(void)
{
    fm_t f;
    fm_init(&f);
    fm_raise(&f, "JESD_FAIL", FM_CRITICAL, "link down");

    char json[1024];
    int n = fm_to_json(&f, json, sizeof(json));
    assert(n > 0);
    assert((size_t)n == strlen(json));
    assert(strstr(json, "o-ran-fm:active-alarm-list"));
    assert(strstr(json, "\"fault-id\": \"JESD_FAIL\""));
    assert(strstr(json, "\"fault-severity\": \"CRITICAL\""));
    assert(strstr(json, "\"is-cleared\": false"));

    /* empty list still produces valid JSON */
    fm_t empty;
    fm_init(&empty);
    n = fm_to_json(&empty, json, sizeof(json));
    assert(n > 0);
    assert(strstr(json, "active-alarms"));
}

int main(void)
{
    test_raise_clear_lifecycle();
    test_reraise_refreshes();
    test_max_severity();
    test_perf_thresholds();
    test_json();
    printf("test_fm: PASS\n");
    return 0;
}
