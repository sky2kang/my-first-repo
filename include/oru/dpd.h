/* SPDX-License-Identifier: MIT */
/*
 * DPD (Digital Pre-Distortion) for the transmit digital front-end.
 *
 * A power amplifier (PA) is most efficient near saturation, but there it is
 * nonlinear: it compresses large amplitudes (AM/AM) and rotates their phase
 * (AM/PM), creating in-band distortion (EVM) and out-of-band emissions
 * (spectral regrowth, ACLR). DPD applies the *inverse* nonlinearity before
 * the PA so the cascade (DPD -> PA) is linear. It sits after CFR in the DFE
 * (... -> beamform -> CFR -> DPD -> PA) and adapts from the PA output
 * captured by the observation receiver (ORX on the ADRV9025).
 *
 * This module uses a memoryless odd-order polynomial model. The PA gain as
 * a function of instantaneous magnitude r = |x| is:
 *
 *     G_pa(r) = c1 + c3 r^2 + c5 r^4 + ...        (complex coefficients)
 *     y = x * G_pa(r)                              (PA output)
 *
 * The pre-distorter applies the inverse gain so that PA(DPD(x)) ~ G0 * x:
 *
 *     x_pd = x * G_dpd(|x|)
 *
 * `dpd_pa_model` lets tests/sim play the PA; `dpd_apply` pre-distorts; and
 * `dpd_adapt` refines the DPD coefficients from a captured (input, PA
 * output) pair by indirect learning. Magnitudes are normalised to full
 * scale (32768) so coefficients are dimensionless.
 */
#ifndef ORU_DPD_H
#define ORU_DPD_H

#include "oru/types.h"

#define DPD_MAX_ORDER 5u    /* highest odd order term (1,3,5)            */
#define DPD_NTAPS     3u    /* number of coefficients: orders 1,3,5      */

/* Complex polynomial coefficients (one per odd order: 1, 3, 5). */
typedef struct {
    double re[DPD_NTAPS];
    double im[DPD_NTAPS];
} dpd_coeffs_t;

/* Initialise to the identity pre-distorter (c1 = 1, rest 0). */
void dpd_init(dpd_coeffs_t *c);

/*
 * Apply a memoryless nonlinear gain model with coefficients `c` to `in`,
 * writing `out` (n samples). Used both as the DPD pre-distorter and, with a
 * PA's coefficients, as the PA model itself. in/out may alias.
 */
oru_status_t dpd_eval(const dpd_coeffs_t *c, const oru_iq16_t *in,
                      oru_iq16_t *out, size_t n);

/* Convenience wrapper: pre-distort `in` into `out` using DPD coeffs. */
oru_status_t dpd_apply(const dpd_coeffs_t *dpd, const oru_iq16_t *in,
                       oru_iq16_t *out, size_t n);

/* Convenience wrapper: model the PA, producing its (distorted) output. */
oru_status_t dpd_pa_model(const dpd_coeffs_t *pa, const oru_iq16_t *in,
                          oru_iq16_t *out, size_t n);

/*
 * Adapt DPD coefficients by one indirect-learning step. Given the DPD
 * input `x` and the observed PA output `y` for the same block, nudge the
 * DPD coefficients to better invert the PA. `mu` is the step size (0..1).
 * Returns the post-update RMS error between a linear reference and y.
 */
double dpd_adapt(dpd_coeffs_t *dpd, const oru_iq16_t *x,
                 const oru_iq16_t *y, size_t n, double mu);

/* Normalised RMS error (EVM-like) between two blocks, in percent. */
double dpd_rms_error_pct(const oru_iq16_t *a, const oru_iq16_t *b, size_t n);

#endif /* ORU_DPD_H */
