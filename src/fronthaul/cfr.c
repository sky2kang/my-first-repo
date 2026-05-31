/* SPDX-License-Identifier: MIT */
#include "oru/cfr.h"
#include "oru/log.h"

#include <math.h>
#include <string.h>

#define TAG "cfr"

oru_status_t cfr_measure(const oru_iq16_t *iq, size_t n, cfr_stats_t *out)
{
    if (!iq || !out)
        return ORU_ERR_PARAM;

    memset(out, 0, sizeof(*out));
    if (n == 0)
        return ORU_OK;

    double peak = 0.0, sum = 0.0;
    for (size_t k = 0; k < n; k++) {
        double p = (double)iq[k].i * iq[k].i + (double)iq[k].q * iq[k].q;
        if (p > peak)
            peak = p;
        sum += p;
    }
    double avg = sum / (double)n;

    out->peak_pow = peak;
    out->avg_pow  = avg;
    out->papr_db  = (avg > 0.0) ? 10.0 * log10(peak / avg) : 0.0;
    return ORU_OK;
}

int cfr_clip(oru_iq16_t *iq, size_t n, double target_papr_db)
{
    if (!iq)
        return ORU_ERR_PARAM;
    if (n == 0)
        return 0;

    /* RMS amplitude of the block. */
    double sum = 0.0;
    for (size_t k = 0; k < n; k++)
        sum += (double)iq[k].i * iq[k].i + (double)iq[k].q * iq[k].q;
    double avg_pow = sum / (double)n;
    if (avg_pow <= 0.0)
        return 0;                       /* all-zero block, nothing to clip */
    double rms = sqrt(avg_pow);

    /* Threshold magnitude for the target PAPR: T = rms * 10^(papr/20). */
    double thresh = rms * pow(10.0, target_papr_db / 20.0);

    int clipped = 0;
    for (size_t k = 0; k < n; k++) {
        double mag = sqrt((double)iq[k].i * iq[k].i +
                          (double)iq[k].q * iq[k].q);
        if (mag > thresh && mag > 0.0) {
            double scale = thresh / mag;
            iq[k].i = (int16_t)lround((double)iq[k].i * scale);
            iq[k].q = (int16_t)lround((double)iq[k].q * scale);
            clipped++;
        }
    }

    LOGD(TAG, "clipped %d/%zu samples at PAPR target %.1f dB (thresh=%.0f)",
         clipped, n, target_papr_db, thresh);
    return clipped;
}
