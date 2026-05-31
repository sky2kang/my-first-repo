/* SPDX-License-Identifier: MIT */
/* Direct tests for the BFP compressor (oru/bfp.h). */
#include "oru/bfp.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* Exact round-trip when every sample already fits in iq_width bits. */
static void test_exact_when_small(void)
{
    enum { NPRB = 3, NRE = NPRB * 12 };
    oru_iq16_t iq[NRE], out[NRE];
    for (int k = 0; k < NRE; k++) {
        iq[k].i = (int16_t)((k % 7) - 3);   /* |val| <= 3 */
        iq[k].q = (int16_t)(3 - (k % 7));
    }

    uint8_t buf[256];
    int wr = bfp_compress(iq, NPRB, 9, buf, sizeof(buf));
    assert(wr == (int)(NPRB * bfp_prb_bytes(9)));

    int rd = bfp_decompress(buf, (size_t)wr, NPRB, 9, out, NRE);
    assert(rd == NRE);
    for (int k = 0; k < NRE; k++) {
        assert(out[k].i == iq[k].i);
        assert(out[k].q == iq[k].q);
    }
}

/* Lossy but bounded round-trip for large amplitudes (exponent > 0). */
static void test_bounded_loss(void)
{
    enum { NPRB = 2, NRE = NPRB * 12 };
    oru_iq16_t iq[NRE], out[NRE];
    for (int k = 0; k < NRE; k++) {
        iq[k].i = (int16_t)(32000 - 100 * k);
        iq[k].q = (int16_t)(-32000 + 100 * k);
    }

    uint8_t buf[256];
    int wr = bfp_compress(iq, NPRB, 9, buf, sizeof(buf));
    assert(wr > 0);
    int rd = bfp_decompress(buf, (size_t)wr, NPRB, 9, out, NRE);
    assert(rd == NRE);

    /* With width 9 and ~32000 peak, exponent is 7 -> step 128. Error from a
     * single right/left shift is strictly < 2^exponent. */
    for (int k = 0; k < NRE; k++) {
        int di = abs((int)out[k].i - (int)iq[k].i);
        int dq = abs((int)out[k].q - (int)iq[k].q);
        assert(di < 128);
        assert(dq < 128);
    }
}

/* Compressed size must be smaller than raw 16-bit for narrow widths. */
static void test_compression_ratio(void)
{
    size_t raw_per_prb = BFP_VALS_PER_PRB * sizeof(int16_t);  /* 48 bytes */
    assert(bfp_prb_bytes(9)  < raw_per_prb);   /* 1 + 27 = 28 < 48 */
    assert(bfp_prb_bytes(8)  < raw_per_prb);   /* 1 + 24 = 25 < 48 */
    assert(bfp_prb_bytes(12) < raw_per_prb);   /* 1 + 36 = 37 < 48 */
}

static void test_param_guards(void)
{
    oru_iq16_t iq[12] = {0};
    uint8_t buf[64];
    assert(bfp_compress(NULL, 1, 9, buf, sizeof(buf)) < 0);
    assert(bfp_compress(iq, 1, 1, buf, sizeof(buf)) < 0);   /* width too small */
    assert(bfp_compress(iq, 1, 17, buf, sizeof(buf)) < 0);  /* width too large */
    assert(bfp_compress(iq, 1, 9, buf, 4) < 0);             /* output too small */
}

int main(void)
{
    test_exact_when_small();
    test_bounded_loss();
    test_compression_ratio();
    test_param_guards();
    printf("test_bfp: PASS\n");
    return 0;
}
