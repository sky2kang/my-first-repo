/* SPDX-License-Identifier: MIT */
#include "oru/mulaw.h"
#include "oru/bitpack.h"
#include "oru/log.h"

#include <math.h>
#include <string.h>

#define TAG "mulaw"

#define MULAW_MU      255.0
#define MULAW_FS      32768.0   /* full-scale magnitude (|int16| range)  */

size_t mulaw_bytes(size_t n_samples, uint8_t iq_width)
{
    /* I and Q, each iq_width bits, rounded up to whole bytes. */
    size_t bits = n_samples * 2u * iq_width;
    return (bits + 7u) / 8u;
}

/* Compress one signed component to `width` bits (sign + magnitude code). */
static uint32_t compand(int32_t x, unsigned width)
{
    double fs_code = (double)((1u << (width - 1u)) - 1u);  /* e.g. w=9 ->255 */
    double mag = fabs((double)x);
    if (mag > MULAW_FS)
        mag = MULAW_FS;

    double y = fs_code * log1p(MULAW_MU * mag / MULAW_FS) / log1p(MULAW_MU);
    uint32_t code = (uint32_t)(y + 0.5);            /* round magnitude */
    if (code > (uint32_t)fs_code)
        code = (uint32_t)fs_code;

    uint32_t sign = (x < 0) ? 1u : 0u;
    return (sign << (width - 1u)) | code;           /* sign in MSB */
}

/* Inverse companding back to a signed 16-bit component. */
static int16_t expand(uint32_t v, unsigned width)
{
    double fs_code = (double)((1u << (width - 1u)) - 1u);
    uint32_t sign = (v >> (width - 1u)) & 1u;
    uint32_t code = v & ((1u << (width - 1u)) - 1u);

    double mag = (MULAW_FS / MULAW_MU) *
                 (pow(1.0 + MULAW_MU, (double)code / fs_code) - 1.0);
    double x = sign ? -mag : mag;
    if (x > 32767.0)  x = 32767.0;
    if (x < -32768.0) x = -32768.0;
    return (int16_t)(x < 0 ? x - 0.5 : x + 0.5);
}

int mulaw_compress(const oru_iq16_t *iq, size_t n_samples, uint8_t iq_width,
                   uint8_t *out, size_t out_len)
{
    if (!iq || !out)
        return ORU_ERR_PARAM;
    if (iq_width < 4 || iq_width > 16)
        return ORU_ERR_PARAM;

    size_t need = mulaw_bytes(n_samples, iq_width);
    if (out_len < need)
        return ORU_ERR_PARAM;

    memset(out, 0, need);
    bitstream_t bs;
    bs_init(&bs, out, need);

    for (size_t k = 0; k < n_samples; k++) {
        if (!bs_put(&bs, compand(iq[k].i, iq_width), iq_width) ||
            !bs_put(&bs, compand(iq[k].q, iq_width), iq_width))
            return ORU_ERR;
    }

    LOGT(TAG, "companded %zu samples @ %u-bit -> %zu bytes (was %zu)",
         n_samples, iq_width, need, n_samples * 2u * sizeof(int16_t));
    return (int)need;
}

int mulaw_decompress(const uint8_t *in, size_t in_len, size_t n_samples,
                     uint8_t iq_width, oru_iq16_t *iq, size_t max_samples)
{
    if (!in || !iq)
        return ORU_ERR_PARAM;
    if (iq_width < 4 || iq_width > 16)
        return ORU_ERR_PARAM;
    if (n_samples > max_samples)
        return ORU_ERR_PARAM;
    if (in_len < mulaw_bytes(n_samples, iq_width))
        return ORU_ERR_PROTO;

    bitstream_t bs;
    bs_init(&bs, (uint8_t *)in, in_len);

    for (size_t k = 0; k < n_samples; k++) {
        uint32_t ci = 0, cq = 0;
        if (!bs_get(&bs, iq_width, &ci) || !bs_get(&bs, iq_width, &cq))
            return ORU_ERR_PROTO;
        iq[k].i = expand(ci, iq_width);
        iq[k].q = expand(cq, iq_width);
    }
    return (int)n_samples;
}
