/* SPDX-License-Identifier: MIT */
#include "oru/fh_sched.h"
#include "oru/log.h"

#include <string.h>

#define TAG "fh_sched"

const char *fh_window_result_str(fh_window_result_t r)
{
    switch (r) {
    case FH_EARLY:   return "EARLY";
    case FH_ON_TIME: return "ON_TIME";
    case FH_LATE:    return "LATE";
    default:         return "?";
    }
}

uint32_t fh_sched_slot_ns(uint32_t scs_hz)
{
    /* A 5G slot is 14 OFDM symbols. Slot duration = 1 ms / 2^mu, where the
     * numerology mu maps 15 kHz -> 0, 30 kHz -> 1, 60 kHz -> 2, ...
     * Equivalently slot_ns = 1e6 ns * (15000 / scs_hz). */
    switch (scs_hz) {
    case 15000:  return 1000000u;  /* 1.000 ms */
    case 30000:  return 500000u;   /* 0.500 ms */
    case 60000:  return 250000u;   /* 0.250 ms */
    case 120000: return 125000u;   /* 0.125 ms */
    default:     return 0u;
    }
}

oru_status_t fh_sched_init(fh_sched_t *s, const fh_sched_cfg_t *cfg,
                           uint32_t scs_hz)
{
    if (!s || !cfg)
        return ORU_ERR_PARAM;

    uint32_t slot_ns = fh_sched_slot_ns(scs_hz);
    if (slot_ns == 0) {
        LOGE(TAG, "unsupported SCS %u Hz", scs_hz);
        return ORU_ERR_PARAM;
    }
    if (cfg->t2a_min_ns > cfg->t2a_max_ns ||
        cfg->ta3_min_ns > cfg->ta3_max_ns) {
        LOGE(TAG, "invalid window bounds (min > max)");
        return ORU_ERR_PARAM;
    }

    memset(s, 0, sizeof(*s));
    s->cfg     = *cfg;
    s->slot_ns = slot_ns;
    LOGI(TAG, "init: slot=%u ns, DL T2a=[%u..%u] UL Ta3=[%u..%u] (ns)",
         slot_ns, cfg->t2a_min_ns, cfg->t2a_max_ns,
         cfg->ta3_min_ns, cfg->ta3_max_ns);
    return ORU_OK;
}

uint64_t fh_sched_slot_boundary(const fh_sched_t *s, uint64_t t0_ns,
                                uint64_t slot)
{
    return t0_ns + slot * (uint64_t)s->slot_ns;
}

fh_window_result_t fh_sched_classify(fh_sched_t *s, fh_direction_t dir,
                                     uint64_t slot_boundary_ns,
                                     uint64_t event_ns)
{
    fh_window_result_t res;

    if (dir == FH_DL) {
        /* DL reception window: [boundary - T2a_max, boundary - T2a_min].
         * Earlier than the window start = EARLY; later than the end = LATE. */
        uint64_t win_open  = slot_boundary_ns - s->cfg.t2a_max_ns;
        uint64_t win_close = slot_boundary_ns - s->cfg.t2a_min_ns;
        if (event_ns < win_open)
            res = FH_EARLY;
        else if (event_ns > win_close)
            res = FH_LATE;
        else
            res = FH_ON_TIME;

        switch (res) {
        case FH_EARLY:   s->stats.dl_early++;   break;
        case FH_ON_TIME: s->stats.dl_on_time++; break;
        case FH_LATE:    s->stats.dl_late++;    break;
        }
    } else {
        /* UL transmit window: [boundary + Ta3_min, boundary + Ta3_max]. */
        uint64_t win_open  = slot_boundary_ns + s->cfg.ta3_min_ns;
        uint64_t win_close = slot_boundary_ns + s->cfg.ta3_max_ns;
        if (event_ns < win_open)
            res = FH_EARLY;
        else if (event_ns > win_close)
            res = FH_LATE;
        else
            res = FH_ON_TIME;

        switch (res) {
        case FH_EARLY:   s->stats.ul_early++;   break;
        case FH_ON_TIME: s->stats.ul_on_time++; break;
        case FH_LATE:    s->stats.ul_late++;    break;
        }
    }

    if (res != FH_ON_TIME)
        LOGD(TAG, "%s packet %s (boundary=%llu event=%llu)",
             dir == FH_DL ? "DL" : "UL", fh_window_result_str(res),
             (unsigned long long)slot_boundary_ns,
             (unsigned long long)event_ns);
    return res;
}
