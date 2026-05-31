/* SPDX-License-Identifier: MIT */
#include "oru/bfp.h"
#include "oru/log.h"

#include <stdlib.h>
#include <string.h>

#define TAG "bfp"

/* --- MSB-first bit packer over a byte buffer ------------------------------ */

typedef struct {
    uint8_t *buf;
    size_t   len;       /* capacity in bytes            */
    size_t   bitpos;    /* current write/read position  */
} bitstream_t;

/* Write `width` low bits of `val` (MSB first). Returns false on overflow. */
static bool bs_put(bitstream_t *bs, uint32_t val, unsigned width)
{
    if (bs->bitpos + width > bs->len * 8u)
        return false;
    for (unsigned i = 0; i < width; i++) {
        unsigned bit = (val >> (width - 1u - i)) & 1u;
        size_t pos = bs->bitpos + i;
        if (bit)
            bs->buf[pos >> 3] |= (uint8_t)(0x80u >> (pos & 7u));
    }
    bs->bitpos += width;
    return true;
}

/* Read `width` bits (MSB first) into *out. Returns false on underflow. */
static bool bs_get(bitstream_t *bs, unsigned width, uint32_t *out)
{
    if (bs->bitpos + width > bs->len * 8u)
        return false;
    uint32_t v = 0;
    for (unsigned i = 0; i < width; i++) {
        size_t pos = bs->bitpos + i;
        unsigned bit = (bs->buf[pos >> 3] >> (7u - (pos & 7u))) & 1u;
        v = (v << 1) | bit;
    }
    bs->bitpos += width;
    *out = v;
    return true;
}

/* --- BFP core ------------------------------------------------------------- */

size_t bfp_prb_bytes(uint8_t iq_width)
{
    /* 1 exponent byte + 24 mantissas of iq_width bits, rounded up. */
    size_t mant_bits = (size_t)BFP_VALS_PER_PRB * iq_width;
    return 1u + (mant_bits + 7u) / 8u;
}

/* Sign-extend the low `width` bits of v to a signed 32-bit value. */
static int32_t sign_extend(uint32_t v, unsigned width)
{
    uint32_t sign = 1u << (width - 1u);
    if (v & sign)
        return (int32_t)(v | ~((1u << width) - 1u));
    return (int32_t)v;
}

/* Pick the block exponent: smallest shift so every value fits in `width`
 * signed bits. max_abs is the largest |value| in the block. */
static uint8_t pick_exponent(int32_t max_abs, unsigned width)
{
    int32_t max_pos = (1 << (width - 1)) - 1;   /* e.g. width=9 -> 255 */
    uint8_t exp = 0;
    while ((max_abs >> exp) > max_pos)
        exp++;
    return exp;
}

int bfp_compress(const oru_iq16_t *iq, size_t n_prb, uint8_t iq_width,
                 uint8_t *out, size_t out_len)
{
    if (!iq || !out)
        return ORU_ERR_PARAM;
    if (iq_width < 2 || iq_width > 16)
        return ORU_ERR_PARAM;

    size_t need = n_prb * bfp_prb_bytes(iq_width);
    if (out_len < need)
        return ORU_ERR_PARAM;

    memset(out, 0, need);

    for (size_t b = 0; b < n_prb; b++) {
        const oru_iq16_t *blk = &iq[b * BFP_RE_PER_PRB];
        uint8_t *blk_out = &out[b * bfp_prb_bytes(iq_width)];

        /* 1. largest magnitude across the 24 values in this PRB */
        int32_t max_abs = 0;
        for (size_t k = 0; k < BFP_RE_PER_PRB; k++) {
            int32_t ai = abs((int32_t)blk[k].i);
            int32_t aq = abs((int32_t)blk[k].q);
            if (ai > max_abs) max_abs = ai;
            if (aq > max_abs) max_abs = aq;
        }

        /* 2. shared exponent */
        uint8_t exp = pick_exponent(max_abs, iq_width);
        blk_out[0] = exp;

        /* 3. pack shifted mantissas */
        bitstream_t bs = { blk_out + 1, bfp_prb_bytes(iq_width) - 1u, 0 };
        for (size_t k = 0; k < BFP_RE_PER_PRB; k++) {
            int32_t mi = (int32_t)blk[k].i >> exp;
            int32_t mq = (int32_t)blk[k].q >> exp;
            uint32_t mask = (iq_width == 32) ? 0xffffffffu
                                             : ((1u << iq_width) - 1u);
            if (!bs_put(&bs, (uint32_t)mi & mask, iq_width) ||
                !bs_put(&bs, (uint32_t)mq & mask, iq_width)) {
                return ORU_ERR;
            }
        }
    }

    LOGT(TAG, "compressed %zu PRB @ %u-bit -> %zu bytes (was %zu)",
         n_prb, iq_width, need, n_prb * BFP_VALS_PER_PRB * sizeof(int16_t));
    return (int)need;
}

int bfp_decompress(const uint8_t *in, size_t in_len, size_t n_prb,
                   uint8_t iq_width, oru_iq16_t *iq, size_t max_samples)
{
    if (!in || !iq)
        return ORU_ERR_PARAM;
    if (iq_width < 2 || iq_width > 16)
        return ORU_ERR_PARAM;

    size_t need = n_prb * bfp_prb_bytes(iq_width);
    if (in_len < need)
        return ORU_ERR_PROTO;
    if (n_prb * BFP_RE_PER_PRB > max_samples)
        return ORU_ERR_PARAM;

    for (size_t b = 0; b < n_prb; b++) {
        const uint8_t *blk_in = &in[b * bfp_prb_bytes(iq_width)];
        oru_iq16_t *blk = &iq[b * BFP_RE_PER_PRB];

        uint8_t exp = blk_in[0];
        bitstream_t bs = { (uint8_t *)(blk_in + 1),
                           bfp_prb_bytes(iq_width) - 1u, 0 };

        for (size_t k = 0; k < BFP_RE_PER_PRB; k++) {
            uint32_t ri = 0, rq = 0;
            if (!bs_get(&bs, iq_width, &ri) ||
                !bs_get(&bs, iq_width, &rq)) {
                return ORU_ERR_PROTO;
            }
            blk[k].i = (int16_t)(sign_extend(ri, iq_width) << exp);
            blk[k].q = (int16_t)(sign_extend(rq, iq_width) << exp);
        }
    }

    return (int)(n_prb * BFP_RE_PER_PRB);
}
