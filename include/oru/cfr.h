/* SPDX-License-Identifier: MIT */
/*
 * CFR (Crest Factor Reduction) for the transmit digital front-end.
 *
 * An OFDM waveform has a high peak-to-average power ratio (PAPR): rare large
 * peaks force the power amplifier (PA) to back off, wasting efficiency. CFR
 * trims those peaks before the PA so the average power can be raised without
 * driving the PA into nonlinear saturation. It runs in the DFE after
 * beamforming and before the PA (conceptually: ... -> beamform -> CFR -> PA).
 *
 * This module implements the simplest, most common CFR: hard clipping with
 * phase preservation. Any sample whose magnitude exceeds a threshold is
 * scaled back to the threshold while keeping its angle:
 *
 *     if |x| > T:  x' = x * (T / |x|)     else x' = x
 *
 * The threshold is expressed relative to the RMS level as a target PAPR in
 * dB, so the same setting adapts to the signal's power. Clipping adds
 * in-band distortion (EVM) and out-of-band emissions; real CFR follows it
 * with peak windowing / filtering, which is left as a TODO. We expose PAPR
 * measurement so the before/after improvement is visible and testable.
 */
#ifndef ORU_CFR_H
#define ORU_CFR_H

#include "oru/types.h"

/* Measured power statistics of an IQ block. */
typedef struct {
    double peak_pow;   /* max |x|^2                                  */
    double avg_pow;    /* mean |x|^2                                 */
    double papr_db;    /* 10*log10(peak/avg)                         */
} cfr_stats_t;

/* Compute PAPR statistics for `n` samples. Returns ORU_OK, or ORU_ERR_PARAM
 * on bad input (out stays zeroed for an all-zero / empty block). */
oru_status_t cfr_measure(const oru_iq16_t *iq, size_t n, cfr_stats_t *out);

/*
 * Clip `iq` in place so the peak magnitude is limited to `target_papr_db`
 * above the RMS level (phase preserved). Returns the number of samples that
 * were clipped, or a negative oru_status_t. A lower target clips harder.
 */
int cfr_clip(oru_iq16_t *iq, size_t n, double target_papr_db);

#endif /* ORU_CFR_H */
