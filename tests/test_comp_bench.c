/* SPDX-License-Identifier: MIT */
/* Tests for the compression benchmark harness (oru/comp_bench.h). */
#include "oru/comp_bench.h"
#include "oru/fronthaul.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

/* A few PRBs of moderate-amplitude IQ. */
static void fill(oru_iq16_t *iq, size_t n)
{
    for (size_t k = 0; k < n; k++) {
        iq[k].i = (int16_t)(1000.0 * sin(0.1 * k));
        iq[k].q = (int16_t)(1000.0 * cos(0.1 * k));
    }
}

static void test_none_lossless(void)
{
    enum { NPRB = 4, NRE = NPRB * 12 };
    oru_iq16_t iq[NRE];
    fill(iq, NRE);

    comp_bench_result_t r;
    assert(comp_bench_run(iq, NPRB, ORAN_COMP_NONE, 16, &r) == ORU_OK);
    assert(r.max_abs_err == 0);
    assert(r.rmse == 0.0);
    /* uncompressed payload equals raw, ratio ~1.0 */
    assert(r.comp_bytes == r.raw_bytes);
    assert(r.ratio > 0.99 && r.ratio < 1.01);
}

static void test_bfp_compresses(void)
{
    enum { NPRB = 4, NRE = NPRB * 12 };
    oru_iq16_t iq[NRE];
    fill(iq, NRE);

    comp_bench_result_t r;
    assert(comp_bench_run(iq, NPRB, ORAN_COMP_BFP, 9, &r) == ORU_OK);
    /* 9-bit BFP must be smaller than raw 16-bit */
    assert(r.comp_bytes < r.raw_bytes);
    assert(r.ratio > 1.0);
    /* amplitude ~1000 needs ~11 bits; at width 9 the block exponent is 2
     * (step 4), so the RMS error is small but nonzero (< one step). */
    assert(r.rmse < 4.0);
    assert(r.max_abs_err < 4);
}

static void test_mulaw_bounded(void)
{
    enum { NPRB = 4, NRE = NPRB * 12 };
    oru_iq16_t iq[NRE];
    fill(iq, NRE);

    comp_bench_result_t r;
    assert(comp_bench_run(iq, NPRB, ORAN_COMP_MULAW, 8, &r) == ORU_OK);
    assert(r.comp_bytes < r.raw_bytes);
    assert(r.ratio > 1.0);
    /* lossy but finite error */
    assert(r.rmse >= 0.0 && r.max_abs_err < 32768);
}

static void test_ratio_ordering(void)
{
    /* Narrower widths should compress more (higher ratio). */
    enum { NPRB = 8, NRE = NPRB * 12 };
    oru_iq16_t iq[NRE];
    fill(iq, NRE);

    comp_bench_result_t r8, r12;
    assert(comp_bench_run(iq, NPRB, ORAN_COMP_BFP, 8, &r8) == ORU_OK);
    assert(comp_bench_run(iq, NPRB, ORAN_COMP_BFP, 12, &r12) == ORU_OK);
    assert(r8.ratio > r12.ratio);
}

int main(void)
{
    test_none_lossless();
    test_bfp_compresses();
    test_mulaw_bounded();
    test_ratio_ordering();
    printf("test_comp_bench: PASS\n");
    return 0;
}
