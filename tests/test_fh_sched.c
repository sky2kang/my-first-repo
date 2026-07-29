/* SPDX-License-Identifier: MIT */
/* Tests for the fronthaul timing-window scheduler (oru/fh_sched.h). */
#include "oru/fh_sched.h"

#include <assert.h>
#include <stdio.h>

static void test_slot_ns(void)
{
    assert(fh_sched_slot_ns(15000)  == 1000000u);
    assert(fh_sched_slot_ns(30000)  == 500000u);
    assert(fh_sched_slot_ns(60000)  == 250000u);
    assert(fh_sched_slot_ns(120000) == 125000u);
    assert(fh_sched_slot_ns(12345)  == 0u);   /* unsupported */
}

static void test_dl_window(void)
{
    /* T2a window: [boundary - 300us, boundary - 100us]. */
    fh_sched_cfg_t cfg = {
        .t2a_min_ns = 100000, .t2a_max_ns = 300000,
        .ta3_min_ns = 50000,  .ta3_max_ns = 200000,
    };
    fh_sched_t s;
    assert(fh_sched_init(&s, &cfg, 30000) == ORU_OK);

    uint64_t t0 = 1000000000ull;             /* arbitrary epoch */
    uint64_t boundary = fh_sched_slot_boundary(&s, t0, 10);
    assert(boundary == t0 + 10ull * 500000u);

    /* window is [boundary-300us, boundary-100us] */
    uint64_t open  = boundary - 300000;
    uint64_t close = boundary - 100000;

    assert(fh_sched_classify(&s, FH_DL, boundary, open - 1)   == FH_EARLY);
    assert(fh_sched_classify(&s, FH_DL, boundary, open)       == FH_ON_TIME);
    assert(fh_sched_classify(&s, FH_DL, boundary, close)      == FH_ON_TIME);
    assert(fh_sched_classify(&s, FH_DL, boundary, close + 1)  == FH_LATE);
    /* mid-window */
    assert(fh_sched_classify(&s, FH_DL, boundary, boundary - 200000) == FH_ON_TIME);

    assert(s.stats.dl_early == 1);
    assert(s.stats.dl_on_time == 3);
    assert(s.stats.dl_late == 1);
}

static void test_ul_window(void)
{
    fh_sched_cfg_t cfg = {
        .t2a_min_ns = 100000, .t2a_max_ns = 300000,
        .ta3_min_ns = 50000,  .ta3_max_ns = 200000,
    };
    fh_sched_t s;
    assert(fh_sched_init(&s, &cfg, 30000) == ORU_OK);

    uint64_t boundary = 5000000ull;
    uint64_t open  = boundary + 50000;
    uint64_t close = boundary + 200000;

    assert(fh_sched_classify(&s, FH_UL, boundary, open - 1)  == FH_EARLY);
    assert(fh_sched_classify(&s, FH_UL, boundary, open)      == FH_ON_TIME);
    assert(fh_sched_classify(&s, FH_UL, boundary, close)     == FH_ON_TIME);
    assert(fh_sched_classify(&s, FH_UL, boundary, close + 1) == FH_LATE);

    assert(s.stats.ul_early == 1);
    assert(s.stats.ul_on_time == 2);
    assert(s.stats.ul_late == 1);
    /* DL counters untouched */
    assert(s.stats.dl_on_time == 0);
}

static void test_init_guards(void)
{
    fh_sched_t s;
    fh_sched_cfg_t bad = {
        .t2a_min_ns = 300000, .t2a_max_ns = 100000,  /* min > max */
        .ta3_min_ns = 50000,  .ta3_max_ns = 200000,
    };
    assert(fh_sched_init(&s, &bad, 30000) == ORU_ERR_PARAM);

    fh_sched_cfg_t ok = {
        .t2a_min_ns = 100000, .t2a_max_ns = 300000,
        .ta3_min_ns = 50000,  .ta3_max_ns = 200000,
    };
    assert(fh_sched_init(&s, &ok, 7000) == ORU_ERR_PARAM);  /* bad SCS */
}

int main(void)
{
    test_slot_ns();
    test_dl_window();
    test_ul_window();
    test_init_guards();
    printf("test_fh_sched: PASS\n");
    return 0;
}
