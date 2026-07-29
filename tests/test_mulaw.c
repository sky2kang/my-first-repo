/* SPDX-License-Identifier: MIT */
/* Tests for µ-law companding (oru/mulaw.h). */
#include "oru/mulaw.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* Round-trip a buffer and return the maximum absolute error. */
static int roundtrip_max_err(const oru_iq16_t *iq, size_t n, uint8_t width)
{
    uint8_t buf[4096];
    int wr = mulaw_compress(iq, n, width, buf, sizeof(buf));
    assert(wr == (int)mulaw_bytes(n, width));

    oru_iq16_t out[256];
    int rd = mulaw_decompress(buf, (size_t)wr, n, width, out, 256);
    assert(rd == (int)n);

    int maxerr = 0;
    for (size_t k = 0; k < n; k++) {
        int di = abs((int)out[k].i - (int)iq[k].i);
        int dq = abs((int)out[k].q - (int)iq[k].q);
        if (di > maxerr) maxerr = di;
        if (dq > maxerr) maxerr = dq;
    }
    return maxerr;
}

static void test_size(void)
{
    /* 12 samples (one PRB), 8-bit -> 12*2*8/8 = 24 bytes; raw is 48. */
    assert(mulaw_bytes(12, 8) == 24);
    assert(mulaw_bytes(12, 8) < 12u * 2u * sizeof(int16_t));
    /* odd bit width rounds up to whole bytes */
    assert(mulaw_bytes(1, 9) == 3);   /* 18 bits -> 3 bytes */
}

static void test_zero_exact(void)
{
    oru_iq16_t iq[12] = {0};
    assert(roundtrip_max_err(iq, 12, 8) == 0);
}

static void test_small_signal_fidelity(void)
{
    /* µ-law devotes more codes to small magnitudes: a low-amplitude signal
     * should reconstruct with small absolute error even at 8-bit. */
    oru_iq16_t iq[24];
    for (int k = 0; k < 24; k++) {
        iq[k].i = (int16_t)(k * 3 - 36);   /* |val| <= 36 */
        iq[k].q = (int16_t)(36 - k * 3);
    }
    int err = roundtrip_max_err(iq, 24, 8);
    assert(err < 8);   /* small near zero */
}

static void test_full_scale_bounded(void)
{
    /* Large magnitudes have coarse steps but must stay sign-correct and
     * within a bounded relative error. */
    oru_iq16_t iq[24];
    for (int k = 0; k < 24; k++) {
        iq[k].i = (int16_t)(30000 - 200 * k);
        iq[k].q = (int16_t)(-30000 + 200 * k);
    }
    uint8_t buf[4096];
    int wr = mulaw_compress(iq, 24, 8, buf, sizeof(buf));
    assert(wr > 0);
    oru_iq16_t out[24];
    assert(mulaw_decompress(buf, (size_t)wr, 24, 8, out, 24) == 24);
    for (int k = 0; k < 24; k++) {
        /* sign preserved */
        assert((out[k].i < 0) == (iq[k].i < 0) || iq[k].i == 0);
        /* within 10% of full scale */
        assert(abs((int)out[k].i - (int)iq[k].i) < 3300);
    }
}

static void test_param_guards(void)
{
    oru_iq16_t iq[12] = {0};
    uint8_t buf[64];
    assert(mulaw_compress(NULL, 12, 8, buf, sizeof(buf)) < 0);
    assert(mulaw_compress(iq, 12, 3, buf, sizeof(buf)) < 0);   /* width < 4 */
    assert(mulaw_compress(iq, 12, 17, buf, sizeof(buf)) < 0);  /* width > 16 */
    assert(mulaw_compress(iq, 12, 8, buf, 4) < 0);             /* out too small */
}

int main(void)
{
    test_size();
    test_zero_exact();
    test_small_signal_fidelity();
    test_full_scale_bounded();
    test_param_guards();
    printf("test_mulaw: PASS\n");
    return 0;
}
