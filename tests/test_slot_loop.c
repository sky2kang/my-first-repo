/* SPDX-License-Identifier: MIT */
/* Tests for the slot-cadence processing loop (oru/slot_loop.h). */
#include "oru/slot_loop.h"
#include "oru/hal.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const fh_sched_cfg_t CFG = {
    .t2a_min_ns = 100000, .t2a_max_ns = 300000,
    .ta3_min_ns = 50000,  .ta3_max_ns = 200000,
};

/* Test context: a virtual clock plus bookkeeping. */
typedef struct {
    uint64_t now;
    uint64_t slot_ns;
    uint64_t work_calls;
    uint64_t last_slot;
    uint64_t last_boundary;
    int      stop_at;        /* slot index to stop at, or -1 */
} test_ctx_t;

static uint64_t clk(void *vctx)
{
    test_ctx_t *c = vctx;
    return c->now;
}

/* Per-slot work: drive one UL build, advancing the virtual clock. */
static oru_status_t work(void *vctx, uint64_t slot, uint64_t boundary,
                         datapath_t *dp)
{
    test_ctx_t *c = vctx;
    c->work_calls++;
    c->last_slot = slot;
    c->last_boundary = boundary;

    /* emit one UL packet "on time" within the Ta3 window */
    uint8_t buf[2048];
    bool late = true;
    int n = datapath_build_ul(dp, 1, ORAN_COMP_BFP, 9, boundary,
                              boundary + 100000, buf, sizeof(buf), &late);
    assert(n > 0);
    assert(!late);

    /* advance virtual time by one slot */
    c->now += c->slot_ns;

    if (c->stop_at >= 0 && (int)slot == c->stop_at)
        return ORU_ERR;   /* request early stop */
    return ORU_OK;
}

static void test_runs_all_slots(void)
{
    datapath_t dp;
    assert(datapath_init(&dp, &CFG, 30000, 0, 0) == ORU_OK);

    test_ctx_t ctx = {0};
    ctx.slot_ns = 500000;     /* 30 kHz */
    ctx.stop_at = -1;

    slot_loop_t lp;
    assert(slot_loop_init(&lp, &dp, clk, work, &ctx, 1000000, 30000) == ORU_OK);
    assert(slot_loop_run(&lp, 10) == ORU_OK);

    assert(ctx.work_calls == 10);
    assert(lp.slots_done == 10);
    assert(dp.stats.ul_sent == 10);
    assert(dp.stats.ul_late == 0);
    /* last boundary = t0 + 9 * slot_ns */
    assert(ctx.last_boundary == 1000000 + 9ull * 500000);
}

static void test_early_stop(void)
{
    datapath_t dp;
    assert(datapath_init(&dp, &CFG, 30000, 0, 0) == ORU_OK);

    test_ctx_t ctx = {0};
    ctx.slot_ns = 500000;
    ctx.stop_at = 3;          /* stop when processing slot index 3 */

    slot_loop_t lp;
    assert(slot_loop_init(&lp, &dp, clk, work, &ctx, 0, 30000) == ORU_OK);
    assert(slot_loop_run(&lp, 100) == ORU_ERR);

    assert(ctx.work_calls == 4);   /* slots 0..3 */
    assert(lp.slots_done == 4);
}

int main(void)
{
    assert(hal_init(NULL) == ORU_OK);
    assert(hal_jesd204_bringup() == ORU_OK);

    test_runs_all_slots();
    test_early_stop();

    hal_shutdown();
    printf("test_slot_loop: PASS\n");
    return 0;
}
