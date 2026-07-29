/* SPDX-License-Identifier: MIT */
/* Tests for CFR (crest factor reduction) (oru/cfr.h). */
#include "oru/cfr.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* OFDM-like signal: sum of several tones. Naturally peaky (high PAPR) but
 * with a stable average, the way a real waveform behaves. */
static void make_ofdm(oru_iq16_t *iq, size_t n)
{
    for (size_t k = 0; k < n; k++) {
        double r = 0.0, im = 0.0;
        for (int t = 1; t <= 6; t++) {
            r  += cos(2.0 * M_PI * t * k / n + t);
            im += sin(2.0 * M_PI * t * k / n + t);
        }
        iq[k].i = (int16_t)(r * 1200.0);
        iq[k].q = (int16_t)(im * 1200.0);
    }
}

static void test_measure_zero(void)
{
    oru_iq16_t z[8] = {0};
    cfr_stats_t s;
    assert(cfr_measure(z, 8, &s) == ORU_OK);
    assert(s.peak_pow == 0.0 && s.avg_pow == 0.0 && s.papr_db == 0.0);
}

static void test_measure_constant(void)
{
    /* constant-magnitude signal: peak == avg -> PAPR ~ 0 dB */
    oru_iq16_t c[16];
    for (int k = 0; k < 16; k++) { c[k].i = 1000; c[k].q = 0; }
    cfr_stats_t s;
    assert(cfr_measure(c, 16, &s) == ORU_OK);
    assert(fabs(s.papr_db) < 0.01);
}

static void test_clip_reduces_papr(void)
{
    enum { N = 256 };
    oru_iq16_t iq[N];
    make_ofdm(iq, N);

    cfr_stats_t before, after;
    assert(cfr_measure(iq, N, &before) == ORU_OK);
    assert(before.papr_db > 7.0);   /* OFDM-like => elevated PAPR */

    int clipped = cfr_clip(iq, N, 6.0);   /* target 6 dB PAPR */
    assert(clipped > 0);

    assert(cfr_measure(iq, N, &after) == ORU_OK);
    /* PAPR must come down toward the target. Hard clipping also lowers the
     * average slightly, so it settles a little above the target. */
    assert(after.papr_db < before.papr_db);
    assert(after.papr_db < 7.0);
}

static void test_clip_preserves_phase(void)
{
    oru_iq16_t iq[1] = { { 9000, 0 } };   /* on the +I axis */
    /* surround with context so RMS is low and this peak gets clipped */
    enum { N = 16 };
    oru_iq16_t buf[N];
    for (int k = 0; k < N; k++) { buf[k].i = 300; buf[k].q = 0; }
    buf[8].i = 9000; buf[8].q = 0;
    (void)iq;

    int clipped = cfr_clip(buf, N, 3.0);
    assert(clipped >= 1);
    /* the clipped peak stays on the +I axis (phase preserved) */
    assert(buf[8].q == 0);
    assert(buf[8].i > 0 && buf[8].i < 9000);
}

static void test_no_clip_when_below_target(void)
{
    /* constant-magnitude signal already has ~0 dB PAPR: a generous target
     * leaves every sample untouched. */
    enum { N = 32 };
    oru_iq16_t iq[N], copy[N];
    for (int k = 0; k < N; k++) { iq[k].i = 1000; iq[k].q = 500; }
    for (int k = 0; k < N; k++) copy[k] = iq[k];

    int clipped = cfr_clip(iq, N, 6.0);
    assert(clipped == 0);
    for (int k = 0; k < N; k++) {
        assert(iq[k].i == copy[k].i && iq[k].q == copy[k].q);
    }
}

int main(void)
{
    test_measure_zero();
    test_measure_constant();
    test_clip_reduces_papr();
    test_clip_preserves_phase();
    test_no_clip_when_below_target();
    printf("test_cfr: PASS\n");
    return 0;
}
