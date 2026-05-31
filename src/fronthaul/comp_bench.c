/* SPDX-License-Identifier: MIT */
#include "oru/comp_bench.h"
#include "oru/fronthaul.h"
#include "oru/log.h"

#include <math.h>
#include <stdlib.h>

#define TAG "comp_bench"

#define BENCH_MAX_PRB     273u
#define BENCH_MAX_SAMPLES (BENCH_MAX_PRB * 12u)

oru_status_t comp_bench_run(const oru_iq16_t *iq, uint16_t n_prb,
                            uint8_t comp_meth, uint8_t iq_width,
                            comp_bench_result_t *out)
{
    if (!iq || !out)
        return ORU_ERR_PARAM;
    if (n_prb == 0 || (size_t)n_prb * 12u > BENCH_MAX_SAMPLES)
        return ORU_ERR_PARAM;

    size_t n = (size_t)n_prb * 12u;

    /* Encode through the U-plane single-section codec. */
    static uint8_t buf[BENCH_MAX_SAMPLES * 4u + 64u];
    oran_uplane_hdr_t h = {
        .num_prb = n_prb, .comp_meth = comp_meth, .iq_bitwidth = iq_width,
    };
    int wr = oran_uplane_encode(&h, iq, n, buf, sizeof(buf));
    if (wr < 0)
        return (oru_status_t)wr;

    /* Decode back. */
    static oru_iq16_t dec[BENCH_MAX_SAMPLES];
    oran_uplane_hdr_t oh;
    size_t got = 0;
    oru_status_t rc = oran_uplane_decode(buf, (size_t)wr, &oh, dec,
                                         BENCH_MAX_SAMPLES, &got);
    if (rc != ORU_OK)
        return rc;
    if (got != n)
        return ORU_ERR;

    /* Measure. The U-plane payload excludes the 10-byte section header, so
     * compare the IQ payload sizes for an apples-to-apples ratio. */
    size_t raw = n * 2u * sizeof(int16_t);
    size_t comp = (size_t)wr - 10u;     /* strip the fixed U-plane header */

    int maxerr = 0;
    double sumsq = 0.0;
    for (size_t k = 0; k < n; k++) {
        int di = (int)dec[k].i - (int)iq[k].i;
        int dq = (int)dec[k].q - (int)iq[k].q;
        if (abs(di) > maxerr) maxerr = abs(di);
        if (abs(dq) > maxerr) maxerr = abs(dq);
        sumsq += (double)di * di + (double)dq * dq;
    }

    out->comp_meth   = comp_meth;
    out->iq_width    = iq_width;
    out->raw_bytes   = raw;
    out->comp_bytes  = comp;
    out->ratio       = comp ? (double)raw / (double)comp : 0.0;
    out->max_abs_err = maxerr;
    out->rmse        = sqrt(sumsq / (double)(n * 2u));

    LOGD(TAG, "meth=%u w=%u ratio=%.2f maxerr=%d rmse=%.1f",
         comp_meth, iq_width, out->ratio, maxerr, out->rmse);
    return ORU_OK;
}
