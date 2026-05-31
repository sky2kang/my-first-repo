/* SPDX-License-Identifier: MIT */
/* Tests for modulation compression (oru/modcomp.h). */
#include "oru/modcomp.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* Build a clean QPSK PRB: all REs on the +/-A grid for a given scaler A. */
static void fill_qpsk(oru_iq16_t *iq, size_t n, int16_t a)
{
    for (size_t k = 0; k < n; k++) {
        iq[k].i = (k & 1) ? a : (int16_t)-a;
        iq[k].q = (k & 2) ? a : (int16_t)-a;
    }
}

static void test_sizes(void)
{
    assert(modcomp_prb_bytes(2) > 0);          /* QPSK */
    assert(modcomp_prb_bytes(4) > 0);          /* 16QAM */
    assert(modcomp_prb_bytes(6) > 0);          /* 64QAM */
    assert(modcomp_prb_bytes(3) == 0);         /* unsupported */
    /* QPSK: 1 + 2 + ceil(12*2/8) = 6 bytes; raw is 48 */
    assert(modcomp_prb_bytes(2) == 6);
    assert(modcomp_prb_bytes(2) < 12u * 2u * sizeof(int16_t));
}

static void test_qpsk_grid_exact(void)
{
    enum { NPRB = 2, NRE = NPRB * 12 };
    oru_iq16_t iq[NRE], out[NRE];
    fill_qpsk(iq, NRE, 1000);

    uint8_t buf[256];
    int wr = modcomp_compress(iq, NPRB, 2, buf, sizeof(buf));
    assert(wr == (int)(NPRB * modcomp_prb_bytes(2)));

    int rd = modcomp_decompress(buf, (size_t)wr, NPRB, 2, out, NRE);
    assert(rd == NRE);

    /* On-grid QPSK reconstructs each point to its scaler magnitude; sign
     * must be exact and magnitude close to the peak scaler. */
    for (int k = 0; k < NRE; k++) {
        assert((out[k].i < 0) == (iq[k].i < 0));
        assert((out[k].q < 0) == (iq[k].q < 0));
        assert(abs((int)out[k].i - (int)iq[k].i) < 64);
        assert(abs((int)out[k].q - (int)iq[k].q) < 64);
    }
}

static void test_16qam_bounded(void)
{
    enum { NPRB = 1, NRE = 12 };
    oru_iq16_t iq[NRE], out[NRE];
    /* 16QAM grid points at +/-1, +/-3 times a unit; use unit=1000 */
    const int pts[4] = { -3000, -1000, 1000, 3000 };
    for (int k = 0; k < NRE; k++) {
        iq[k].i = (int16_t)pts[k % 4];
        iq[k].q = (int16_t)pts[(k + 1) % 4];
    }

    uint8_t buf[256];
    int wr = modcomp_compress(iq, NPRB, 4, buf, sizeof(buf));
    assert(wr > 0);
    int rd = modcomp_decompress(buf, (size_t)wr, NPRB, 4, out, NRE);
    assert(rd == NRE);

    for (int k = 0; k < NRE; k++) {
        /* nearest-grid reconstruction within one step (~2*unit = 2000) */
        assert(abs((int)out[k].i - (int)iq[k].i) <= 1000);
        assert(abs((int)out[k].q - (int)iq[k].q) <= 1000);
    }
}

static void test_param_guards(void)
{
    oru_iq16_t iq[12] = {0};
    uint8_t buf[64];
    assert(modcomp_compress(NULL, 1, 2, buf, sizeof(buf)) < 0);
    assert(modcomp_compress(iq, 1, 3, buf, sizeof(buf)) < 0);  /* bad order */
    assert(modcomp_compress(iq, 1, 2, buf, 2) < 0);            /* out small */
}

int main(void)
{
    test_sizes();
    test_qpsk_grid_exact();
    test_16qam_bounded();
    test_param_guards();
    printf("test_modcomp: PASS\n");
    return 0;
}
