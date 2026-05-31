/* SPDX-License-Identifier: MIT */
#include "oru/modcomp.h"
#include "oru/bitpack.h"
#include "oru/log.h"

#include <stdlib.h>
#include <string.h>

#define TAG "modcomp"

/* Supported modulation orders (bits per symbol). QAM are square grids whose
 * per-axis level count is 2^(mod_bits/2). BPSK/QPSK handled as 1D/per-axis. */
static int mod_bits_ok(uint8_t b)
{
    return b == 1 || b == 2 || b == 4 || b == 6 || b == 8;
}

/* Per-axis level count for a square QAM of `mod_bits` (e.g. 16QAM -> 4). */
static unsigned axis_levels(uint8_t mod_bits)
{
    /* BPSK(1)/QPSK(2): 2 levels per axis; 16QAM(4):4; 64QAM(6):8; 256QAM(8):16 */
    unsigned per_axis_bits = (mod_bits <= 2) ? 1u : (unsigned)(mod_bits / 2u);
    return 1u << per_axis_bits;
}

size_t modcomp_prb_bytes(uint8_t mod_bits)
{
    if (!mod_bits_ok(mod_bits))
        return 0;
    /* 1 byte (csf+order) + 2 bytes scaler + 12 indices of mod_bits each. */
    size_t idx_bits = (size_t)MODCOMP_RE_PER_PRB * mod_bits;
    return 1u + 2u + (idx_bits + 7u) / 8u;
}

/* Map a signed component to the nearest constellation level index, and back.
 * Levels are symmetric: for L levels the points are -(L-1), .., -1, +1, ..,
 * +(L-1) in odd steps, scaled so the outermost point maps to `scaler`. */
static uint32_t quantize_axis(int32_t v, unsigned levels, int32_t scaler)
{
    /* index in [0, levels-1]; level value = (2*index - (levels-1)). */
    int32_t maxlvl = (int32_t)levels - 1;          /* outermost odd point */
    if (maxlvl <= 0 || scaler <= 0)
        return 0;
    /* normalise v to the odd-integer grid */
    double step = (double)scaler / (double)maxlvl;
    double g = (double)v / step;                   /* ~ in [-(L-1), +(L-1)] */
    int32_t lvl = (int32_t)((g + (double)maxlvl) / 2.0 + 0.5); /* to index  */
    if (lvl < 0) lvl = 0;
    if (lvl > maxlvl) lvl = maxlvl;
    return (uint32_t)lvl;
}

static int16_t dequantize_axis(uint32_t idx, unsigned levels, int32_t scaler)
{
    int32_t maxlvl = (int32_t)levels - 1;
    if (maxlvl <= 0)
        return 0;
    int32_t odd = 2 * (int32_t)idx - maxlvl;       /* odd-integer position  */
    double step = (double)scaler / (double)maxlvl;
    double v = (double)odd * step;
    if (v > 32767.0)  v = 32767.0;
    if (v < -32768.0) v = -32768.0;
    return (int16_t)(v < 0 ? v - 0.5 : v + 0.5);
}

int modcomp_compress(const oru_iq16_t *iq, size_t n_prb, uint8_t mod_bits,
                     uint8_t *out, size_t out_len)
{
    if (!iq || !out)
        return ORU_ERR_PARAM;
    if (!mod_bits_ok(mod_bits))
        return ORU_ERR_PARAM;

    size_t per = modcomp_prb_bytes(mod_bits);
    if (out_len < n_prb * per)
        return ORU_ERR_PARAM;

    unsigned levels = axis_levels(mod_bits);
    memset(out, 0, n_prb * per);

    for (size_t b = 0; b < n_prb; b++) {
        const oru_iq16_t *blk = &iq[b * MODCOMP_RE_PER_PRB];
        uint8_t *blk_out = &out[b * per];

        /* block scaler = peak magnitude across I/Q of the PRB */
        int32_t peak = 1;
        for (size_t k = 0; k < MODCOMP_RE_PER_PRB; k++) {
            int32_t ai = abs((int32_t)blk[k].i);
            int32_t aq = abs((int32_t)blk[k].q);
            if (ai > peak) peak = ai;
            if (aq > peak) peak = aq;
        }

        blk_out[0] = (uint8_t)(mod_bits & 0x0f);
        blk_out[1] = (uint8_t)(peak >> 8);
        blk_out[2] = (uint8_t)(peak & 0xff);

        bitstream_t bs;
        bs_init(&bs, blk_out + 3, per - 3u);
        unsigned per_axis_bits = (mod_bits <= 2) ? 1u : (unsigned)(mod_bits / 2u);
        for (size_t k = 0; k < MODCOMP_RE_PER_PRB; k++) {
            uint32_t qi = quantize_axis(blk[k].i, levels, peak);
            uint32_t qq = quantize_axis(blk[k].q, levels, peak);
            if (mod_bits == 1) {           /* BPSK: 1 bit total, sign of I  */
                if (!bs_put(&bs, qi ? 1u : 0u, 1u))
                    return ORU_ERR;
            } else {
                if (!bs_put(&bs, qi, per_axis_bits) ||
                    !bs_put(&bs, qq, per_axis_bits))
                    return ORU_ERR;
            }
        }
    }

    LOGT(TAG, "mod-compressed %zu PRB @ %u-bit -> %zu bytes",
         n_prb, mod_bits, n_prb * per);
    return (int)(n_prb * per);
}

int modcomp_decompress(const uint8_t *in, size_t in_len, size_t n_prb,
                       uint8_t mod_bits, oru_iq16_t *iq, size_t max_samples)
{
    if (!in || !iq)
        return ORU_ERR_PARAM;
    if (!mod_bits_ok(mod_bits))
        return ORU_ERR_PARAM;

    size_t per = modcomp_prb_bytes(mod_bits);
    if (in_len < n_prb * per)
        return ORU_ERR_PROTO;
    if (n_prb * MODCOMP_RE_PER_PRB > max_samples)
        return ORU_ERR_PARAM;

    unsigned levels = axis_levels(mod_bits);
    unsigned per_axis_bits = (mod_bits <= 2) ? 1u : (unsigned)(mod_bits / 2u);

    for (size_t b = 0; b < n_prb; b++) {
        const uint8_t *blk_in = &in[b * per];
        oru_iq16_t *blk = &iq[b * MODCOMP_RE_PER_PRB];

        int32_t scaler = ((int32_t)blk_in[1] << 8) | (int32_t)blk_in[2];

        bitstream_t bs;
        bs_init(&bs, (uint8_t *)(blk_in + 3), per - 3u);
        for (size_t k = 0; k < MODCOMP_RE_PER_PRB; k++) {
            if (mod_bits == 1) {
                uint32_t bit = 0;
                if (!bs_get(&bs, 1u, &bit))
                    return ORU_ERR_PROTO;
                blk[k].i = dequantize_axis(bit, levels, scaler);
                blk[k].q = 0;
            } else {
                uint32_t qi = 0, qq = 0;
                if (!bs_get(&bs, per_axis_bits, &qi) ||
                    !bs_get(&bs, per_axis_bits, &qq))
                    return ORU_ERR_PROTO;
                blk[k].i = dequantize_axis(qi, levels, scaler);
                blk[k].q = dequantize_axis(qq, levels, scaler);
            }
        }
    }
    return (int)(n_prb * MODCOMP_RE_PER_PRB);
}
