/* SPDX-License-Identifier: MIT */
#include "oru/datapath.h"
#include "oru/hal.h"
#include "oru/log.h"

#include <string.h>

#define TAG "datapath"

/* Largest U-plane section we buffer in the datapath (PRBs * 12 REs). */
#define DP_MAX_PRB     273u
#define DP_MAX_SAMPLES (DP_MAX_PRB * 12u)

oru_status_t datapath_init(datapath_t *dp, const fh_sched_cfg_t *cfg,
                           uint32_t scs_hz, uint8_t tx_chan, uint8_t rx_chan)
{
    if (!dp || !cfg)
        return ORU_ERR_PARAM;

    memset(dp, 0, sizeof(*dp));
    oru_status_t rc = fh_sched_init(&dp->sched, cfg, scs_hz);
    if (rc != ORU_OK)
        return rc;
    dp->tx_chan = tx_chan;
    dp->rx_chan = rx_chan;
    LOGI(TAG, "init: tx_chan=%u rx_chan=%u", tx_chan, rx_chan);
    return ORU_OK;
}

oru_status_t datapath_handle_dl(datapath_t *dp, const uint8_t *buf,
                                size_t len, uint64_t slot_boundary_ns,
                                uint64_t arrival_ns)
{
    if (!dp || !buf)
        return ORU_ERR_PARAM;

    fh_window_result_t r = fh_sched_classify(&dp->sched, FH_DL,
                                             slot_boundary_ns, arrival_ns);
    if (r == FH_LATE) {
        /* The slot boundary has passed; this IQ can no longer be radiated. */
        dp->stats.dl_dropped_late++;
        LOGD(TAG, "DL packet dropped (LATE), total dropped=%llu",
             (unsigned long long)dp->stats.dl_dropped_late);
        return ORU_ERR_TIMEOUT;
    }

    /* EARLY or ON_TIME: decode and forward to the RF front-end. EARLY would
     * be buffered until its slot in a real RU; here we forward immediately. */
    oran_uplane_hdr_t hdr;
    oru_iq16_t iq[DP_MAX_SAMPLES];
    size_t n = 0;
    oru_status_t rc = oran_uplane_decode(buf, len, &hdr, iq,
                                         DP_MAX_SAMPLES, &n);
    if (rc != ORU_OK) {
        LOGE(TAG, "DL decode failed: %s", oru_status_str(rc));
        return rc;
    }

    rc = hal_tx_iq(dp->tx_chan, iq, n);
    if (rc != ORU_OK) {
        LOGE(TAG, "hal_tx_iq failed: %s", oru_status_str(rc));
        return rc;
    }

    dp->stats.dl_delivered++;
    return ORU_OK;
}

int datapath_build_ul(datapath_t *dp, uint16_t num_prb, uint8_t comp_meth,
                      uint8_t iq_width, uint64_t slot_boundary_ns,
                      uint64_t send_ns, uint8_t *buf, size_t len,
                      bool *was_late)
{
    if (!dp || !buf || !was_late)
        return ORU_ERR_PARAM;
    if ((size_t)num_prb * 12u > DP_MAX_SAMPLES)
        return ORU_ERR_PARAM;

    /* Capture RX IQ from the front-end. */
    oru_iq16_t iq[DP_MAX_SAMPLES];
    size_t n = (size_t)num_prb * 12u;
    oru_status_t rc = hal_rx_iq(dp->rx_chan, iq, n);
    if (rc != ORU_OK) {
        LOGE(TAG, "hal_rx_iq failed: %s", oru_status_str(rc));
        return rc;
    }

    /* Check the transmission deadline (Ta3) but still emit the packet:
     * dropping UL would lose user data; instead we flag it for fault mgmt. */
    fh_window_result_t r = fh_sched_classify(&dp->sched, FH_UL,
                                             slot_boundary_ns, send_ns);
    *was_late = (r == FH_LATE);
    if (*was_late) {
        dp->stats.ul_late++;
        LOGD(TAG, "UL packet past Ta3 deadline, total late=%llu",
             (unsigned long long)dp->stats.ul_late);
    }

    oran_uplane_hdr_t hdr = {
        .frame_id = 0, .subframe_id = 0, .slot_id = 0, .symbol_id = 0,
        .start_prb = 0, .num_prb = num_prb,
        .comp_meth = comp_meth, .iq_bitwidth = iq_width,
    };
    int wr = oran_uplane_encode(&hdr, iq, n, buf, len);
    if (wr < 0)
        return wr;

    dp->stats.ul_sent++;
    return wr;
}
