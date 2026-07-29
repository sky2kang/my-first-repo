/* SPDX-License-Identifier: MIT */
/* Tests for DPD (digital pre-distortion) (oru/dpd.h). */
#include "oru/dpd.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

enum { N = 512 };

static void make_multitone(oru_iq16_t *x, size_t n, double amp)
{
    for (size_t k = 0; k < n; k++) {
        double r = 0.0, im = 0.0;
        for (int t = 1; t <= 4; t++) {
            r  += cos(2.0 * M_PI * t * k / n + t);
            im += sin(2.0 * M_PI * t * k / n + t);
        }
        x[k].i = (int16_t)(r * amp);
        x[k].q = (int16_t)(im * amp);
    }
}

/* A compressive PA: unity small-signal gain, negative odd-order terms. */
static dpd_coeffs_t make_pa(void)
{
    dpd_coeffs_t pa;
    dpd_init(&pa);
    pa.re[0] = 1.0;
    pa.re[1] = -0.30;   /* AM/AM compression */
    pa.re[2] = 0.05;
    pa.im[1] = 0.05;    /* a little AM/PM */
    return pa;
}

static void test_identity_passthrough(void)
{
    dpd_coeffs_t id;
    dpd_init(&id);

    oru_iq16_t x[N], y[N];
    make_multitone(x, N, 2000.0);
    assert(dpd_eval(&id, x, y, N) == ORU_OK);
    /* identity gain reproduces input within rounding */
    for (size_t k = 0; k < N; k++) {
        assert(abs(y[k].i - x[k].i) <= 1);
        assert(abs(y[k].q - x[k].q) <= 1);
    }
    assert(dpd_rms_error_pct(x, y, N) < 0.1);
}

static void test_pa_distorts(void)
{
    dpd_coeffs_t pa = make_pa();
    oru_iq16_t x[N], y[N];
    make_multitone(x, N, 3000.0);
    assert(dpd_pa_model(&pa, x, y, N) == ORU_OK);
    /* the PA noticeably distorts vs the linear (identity) reference */
    double evm = dpd_rms_error_pct(x, y, N);
    assert(evm > 1.0);
}

static void test_dpd_linearises(void)
{
    dpd_coeffs_t pa = make_pa();
    oru_iq16_t x[N], pd[N], y[N];
    make_multitone(x, N, 3000.0);

    /* PA-only EVM (no DPD) */
    dpd_pa_model(&pa, x, y, N);
    double evm_before = dpd_rms_error_pct(x, y, N);

    /* train DPD via indirect learning */
    dpd_coeffs_t dpd;
    dpd_init(&dpd);
    double last_err = 100.0;
    for (int it = 0; it < 200; it++) {
        dpd_apply(&dpd, x, pd, N);
        dpd_pa_model(&pa, pd, y, N);
        last_err = dpd_adapt(&dpd, x, y, N, 0.5);
    }
    assert(last_err < 0.5);   /* training converged */

    /* cascade EVM after DPD */
    dpd_apply(&dpd, x, pd, N);
    dpd_pa_model(&pa, pd, y, N);
    double evm_after = dpd_rms_error_pct(x, y, N);

    /* DPD must reduce the distortion meaningfully */
    assert(evm_after < evm_before);
    assert(evm_after < 0.8 * evm_before);

    /* DPD c3 should oppose the PA's negative c3 (pre-expansion) */
    assert(dpd.re[1] > 0.0);
}

static void test_param_guards(void)
{
    oru_iq16_t x[4] = {0}, y[4];
    dpd_coeffs_t c; dpd_init(&c);
    assert(dpd_eval(NULL, x, y, 4) == ORU_ERR_PARAM);
    assert(dpd_eval(&c, NULL, y, 4) == ORU_ERR_PARAM);
    assert(dpd_rms_error_pct(x, y, 0) == 0.0);
}

int main(void)
{
    test_identity_passthrough();
    test_pa_distorts();
    test_dpd_linearises();
    test_param_guards();
    printf("test_dpd: PASS\n");
    return 0;
}
