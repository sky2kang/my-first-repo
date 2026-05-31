/* SPDX-License-Identifier: MIT */
#include "oru/slot_loop.h"
#include "oru/fh_sched.h"
#include "oru/log.h"

#include <string.h>

#define TAG "slot_loop"

oru_status_t slot_loop_init(slot_loop_t *lp, datapath_t *dp,
                            slot_clock_fn clock, slot_work_fn work, void *ctx,
                            uint64_t t0_ns, uint32_t scs_hz)
{
    if (!lp || !dp || !clock || !work)
        return ORU_ERR_PARAM;

    uint32_t slot_ns = fh_sched_slot_ns(scs_hz);
    if (slot_ns == 0)
        return ORU_ERR_PARAM;

    memset(lp, 0, sizeof(*lp));
    lp->dp      = dp;
    lp->clock   = clock;
    lp->work    = work;
    lp->ctx     = ctx;
    lp->t0_ns   = t0_ns;
    lp->slot_ns = slot_ns;
    LOGI(TAG, "init: t0=%llu slot=%u ns",
         (unsigned long long)t0_ns, slot_ns);
    return ORU_OK;
}

oru_status_t slot_loop_run(slot_loop_t *lp, uint64_t max_slots)
{
    if (!lp)
        return ORU_ERR_PARAM;

    oru_status_t rc = ORU_OK;
    for (uint64_t s = 0; s < max_slots; s++) {
        uint64_t boundary = lp->t0_ns + s * (uint64_t)lp->slot_ns;

        /* In a real build we would block until now >= boundary - lead time.
         * Here we just sample the clock so a test/SIM clock can advance time
         * deterministically (and a real clock reflects actual lateness). */
        uint64_t now = lp->clock(lp->ctx);
        (void)now;

        rc = lp->work(lp->ctx, s, boundary, lp->dp);
        lp->slots_done++;
        if (rc != ORU_OK) {
            LOGD(TAG, "work stopped loop at slot %llu: %s",
                 (unsigned long long)s, oru_status_str(rc));
            break;
        }
    }

    LOGI(TAG, "ran %llu slot(s)", (unsigned long long)lp->slots_done);
    return rc;
}
