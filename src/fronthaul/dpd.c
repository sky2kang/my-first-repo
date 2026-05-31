/* SPDX-License-Identifier: MIT */
#include "oru/dpd.h"
#include "oru/log.h"

#include <math.h>
#include <string.h>

#define TAG "dpd"

#define DPD_FS 32768.0   /* full-scale magnitude for normalisation */

void dpd_init(dpd_coeffs_t *c)
{
    if (!c)
        return;
    memset(c, 0, sizeof(*c));
    c->re[0] = 1.0;   /* identity gain: out = in * 1.0 */
}

/* Complex gain G = sum_k (re[k] + j im[k]) * p^k, where p = (|x|/FS)^2. */
static void poly_gain(const dpd_coeffs_t *c, double p, double *gre, double *gim)
{
    double gr = 0.0, gi = 0.0, pk = 1.0;
    for (unsigned k = 0; k < DPD_NTAPS; k++) {
        gr += c->re[k] * pk;
        gi += c->im[k] * pk;
        pk *= p;
    }
    *gre = gr;
    *gim = gi;
}

static int16_t sat16(double v)
{
    if (v > 32767.0)  return 32767;
    if (v < -32768.0) return -32768;
    return (int16_t)lround(v);
}

oru_status_t dpd_eval(const dpd_coeffs_t *c, const oru_iq16_t *in,
                      oru_iq16_t *out, size_t n)
{
    if (!c || !in || !out)
        return ORU_ERR_PARAM;

    for (size_t k = 0; k < n; k++) {
        double xr = (double)in[k].i, xi = (double)in[k].q;
        double r = sqrt(xr * xr + xi * xi) / DPD_FS;
        double p = r * r;
        double gr, gi;
        poly_gain(c, p, &gr, &gi);
        /* out = x * G */
        out[k].i = sat16(xr * gr - xi * gi);
        out[k].q = sat16(xr * gi + xi * gr);
    }
    return ORU_OK;
}

oru_status_t dpd_apply(const dpd_coeffs_t *dpd, const oru_iq16_t *in,
                       oru_iq16_t *out, size_t n)
{
    return dpd_eval(dpd, in, out, n);
}

oru_status_t dpd_pa_model(const dpd_coeffs_t *pa, const oru_iq16_t *in,
                          oru_iq16_t *out, size_t n)
{
    return dpd_eval(pa, in, out, n);
}

double dpd_rms_error_pct(const oru_iq16_t *a, const oru_iq16_t *b, size_t n)
{
    if (!a || !b || n == 0)
        return 0.0;
    double err = 0.0, ref = 0.0;
    for (size_t k = 0; k < n; k++) {
        double di = (double)a[k].i - b[k].i;
        double dq = (double)a[k].q - b[k].q;
        err += di * di + dq * dq;
        ref += (double)a[k].i * a[k].i + (double)a[k].q * a[k].q;
    }
    if (ref <= 0.0)
        return 0.0;
    return 100.0 * sqrt(err / ref);
}

/*
 * Indirect-learning update. `x` is the signal fed into the PA (the current
 * DPD output), `y` is the observed PA output. We train a post-inverse G so
 * that G(y) ~ x, using LMS on the polynomial basis, then copy G into the
 * pre-distorter (post-inverse == pre-inverse for memoryless DPD).
 *
 *   G(y) = y * (g1 + g3 (|y|/FS)^2 + g5 (|y|/FS)^4)
 *   basis: phi_k = y * (|y|/FS)^(2k)
 *   e = x - G(y);   g_k += mu * e * conj(phi_k) / FS^2
 *
 * Returns the post-update training RMS error (percent), which decreases as
 * the DPD converges to the PA inverse.
 */
double dpd_adapt(dpd_coeffs_t *dpd, const oru_iq16_t *x,
                 const oru_iq16_t *y, size_t n, double mu)
{
    if (!dpd || !x || !y || n == 0)
        return 0.0;

    for (size_t k = 0; k < n; k++) {
        double yr = (double)y[k].i, yi = (double)y[k].q;
        double p = (yr * yr + yi * yi) / (DPD_FS * DPD_FS);

        /* current post-inverse output G(y) */
        double gr, gi;
        poly_gain(dpd, p, &gr, &gi);
        double gyr = yr * gr - yi * gi;
        double gyi = yr * gi + yi * gr;

        /* error e = x - G(y) */
        double er = (double)x[k].i - gyr;
        double ei = (double)x[k].q - gyi;

        /* update each tap: phi_t = y * p^t */
        double pt = 1.0;
        for (unsigned t = 0; t < DPD_NTAPS; t++) {
            double phr = yr * pt;       /* Re(phi_t) */
            double phi = yi * pt;       /* Im(phi_t) */
            /* e * conj(phi) = (er+j ei)(phr - j phi) */
            double ur = er * phr + ei * phi;
            double ui = ei * phr - er * phi;
            double scale = mu / (DPD_FS * DPD_FS);
            dpd->re[t] += scale * ur;
            dpd->im[t] += scale * ui;
            pt *= p;
        }
    }

    /* report residual training error */
    static oru_iq16_t gtmp[4096];
    size_t m = (n < 4096) ? n : 4096;
    dpd_eval(dpd, y, gtmp, m);
    return dpd_rms_error_pct(x, gtmp, m);
}
