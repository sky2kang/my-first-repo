/* SPDX-License-Identifier: MIT */
#include "oru/prach.h"
#include "oru/log.h"

#include <math.h>
#include <string.h>

#define TAG "prach"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Map a logical root index to a physical ZC root u. For this teaching model
 * we use the logical index directly, clamped to a valid nonzero root in
 * [1, Nzc-1] (ZC requires gcd(u, Nzc) = 1; Nzc=139 is prime so any
 * 1..138 works). */
static uint16_t phys_root(uint16_t root)
{
    uint16_t u = (uint16_t)(root % PRACH_NZC);
    if (u == 0)
        u = 1;
    return u;
}

/* x_u[n] = exp(-j * pi * u * n * (n+1) / Nzc), optionally cyclically shifted. */
static void zc_sequence(uint16_t u, uint16_t shift, double *re, double *im)
{
    for (uint16_t n = 0; n < PRACH_NZC; n++) {
        uint16_t idx = (uint16_t)((n + shift) % PRACH_NZC);
        double phase = -M_PI * (double)u * (double)idx * (double)(idx + 1)
                       / (double)PRACH_NZC;
        re[n] = cos(phase);
        im[n] = sin(phase);
    }
}

oru_status_t prach_gen_preamble(uint16_t root, uint16_t shift,
                                int16_t amplitude, oru_iq16_t *out)
{
    if (!out)
        return ORU_ERR_PARAM;

    double re[PRACH_NZC], im[PRACH_NZC];
    zc_sequence(phys_root(root), (uint16_t)(shift % PRACH_NZC), re, im);

    for (uint16_t n = 0; n < PRACH_NZC; n++) {
        out[n].i = (int16_t)lround(re[n] * (double)amplitude);
        out[n].q = (int16_t)lround(im[n] * (double)amplitude);
    }
    return ORU_OK;
}

oru_status_t prach_detect(const oru_iq16_t *rx, uint16_t root,
                          double threshold, prach_detect_t *out)
{
    if (!rx || !out)
        return ORU_ERR_PARAM;

    /* Reference ZC sequence (no shift) for this root. */
    double rre[PRACH_NZC], rim[PRACH_NZC];
    zc_sequence(phys_root(root), 0, rre, rim);

    /* Cyclic cross-correlation magnitude at each shift d:
     *   c[d] = sum_n rx[n] * conj(zc[(n+d) mod Nzc])
     * Peak shift indicates the preamble's cyclic shift. */
    double best_mag = 0.0;
    uint16_t best_shift = 0;
    double sum_mag = 0.0;

    for (uint16_t d = 0; d < PRACH_NZC; d++) {
        double cr = 0.0, ci = 0.0;
        for (uint16_t n = 0; n < PRACH_NZC; n++) {
            uint16_t k = (uint16_t)((n + d) % PRACH_NZC);
            double xr = (double)rx[n].i, xi = (double)rx[n].q;
            /* conj(zc[k]) = (rre[k], -rim[k]) */
            cr += xr * rre[k] + xi * rim[k];
            ci += xi * rre[k] - xr * rim[k];
        }
        double mag = sqrt(cr * cr + ci * ci);
        sum_mag += mag;
        if (mag > best_mag) {
            best_mag = mag;
            best_shift = d;
        }
    }

    double mean = sum_mag / (double)PRACH_NZC;
    double ratio = (mean > 0.0) ? best_mag / mean : 0.0;

    out->peak     = best_mag;
    out->mean     = mean;
    out->ratio    = ratio;
    out->shift    = best_shift;
    out->detected = (ratio >= threshold);

    LOGD(TAG, "root=%u peak=%.0f mean=%.0f ratio=%.1f shift=%u %s",
         root, best_mag, mean, ratio, best_shift,
         out->detected ? "DETECTED" : "-");
    return ORU_OK;
}
