/* SPDX-License-Identifier: MIT */
/* Tests for PRACH preamble generation and detection (oru/prach.h). */
#include "oru/prach.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* Simple deterministic PRNG so the test is reproducible. */
static uint32_t rng_state = 12345u;
static int noise(int amp)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return (int)((rng_state >> 16) % (uint32_t)(2 * amp + 1)) - amp;
}

static void test_detect_clean(void)
{
    oru_iq16_t pre[PRACH_NZC];
    assert(prach_gen_preamble(17, 40, 4000, pre) == ORU_OK);

    prach_detect_t d;
    assert(prach_detect(pre, 17, 5.0, &d) == ORU_OK);
    assert(d.detected);
    /* clean sequence: a single sharp peak, very high ratio */
    assert(d.ratio > 10.0);
    /* the detected cyclic shift recovers the generated shift */
    assert(d.shift == 40);
}

static void test_zero_shift(void)
{
    oru_iq16_t pre[PRACH_NZC];
    assert(prach_gen_preamble(3, 0, 4000, pre) == ORU_OK);

    prach_detect_t d;
    assert(prach_detect(pre, 3, 5.0, &d) == ORU_OK);
    assert(d.detected);
    assert(d.shift == 0);
}

static void test_detect_with_noise(void)
{
    oru_iq16_t pre[PRACH_NZC];
    assert(prach_gen_preamble(29, 12, 4000, pre) == ORU_OK);

    /* add moderate noise (~12% of amplitude) */
    for (uint16_t n = 0; n < PRACH_NZC; n++) {
        pre[n].i = (int16_t)(pre[n].i + noise(500));
        pre[n].q = (int16_t)(pre[n].q + noise(500));
    }

    prach_detect_t d;
    assert(prach_detect(pre, 29, 5.0, &d) == ORU_OK);
    assert(d.detected);
    assert(d.shift == 12);
}

static void test_wrong_root_no_detect(void)
{
    oru_iq16_t pre[PRACH_NZC];
    assert(prach_gen_preamble(50, 20, 4000, pre) == ORU_OK);

    /* correlate against a different root: no impulse -> low ratio */
    prach_detect_t d;
    assert(prach_detect(pre, 90, 5.0, &d) == ORU_OK);
    assert(!d.detected);
    assert(d.ratio < 5.0);
}

static void test_noise_only(void)
{
    oru_iq16_t rx[PRACH_NZC];
    for (uint16_t n = 0; n < PRACH_NZC; n++) {
        rx[n].i = (int16_t)noise(1000);
        rx[n].q = (int16_t)noise(1000);
    }
    prach_detect_t d;
    assert(prach_detect(rx, 7, 5.0, &d) == ORU_OK);
    assert(!d.detected);   /* nothing there */
}

int main(void)
{
    test_detect_clean();
    test_zero_shift();
    test_detect_with_noise();
    test_wrong_root_no_detect();
    test_noise_only();
    printf("test_prach: PASS\n");
    return 0;
}
