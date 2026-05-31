/* SPDX-License-Identifier: MIT */
/*
 * O-RAN U-plane: frequency-domain IQ transport.
 *
 * Two compression methods are supported via hdr->comp_meth:
 *   - ORAN_COMP_NONE: raw 16-bit I/Q, exact round-trip.
 *   - ORAN_COMP_BFP : per-PRB block floating point (see oru/bfp.h), the
 *                     compression O-RAN deployments use most.
 * The header byte layout below is a simplified subset of the real O-RAN
 * U-plane section (radio-app header + section header + udCompHdr).
 */
#include "oru/fronthaul.h"
#include "oru/bfp.h"
#include "oru/log.h"

#include <string.h>

#define TAG "uplane"

/* Header wire layout (10 bytes):
 *   0: frameId
 *   1: subframeId
 *   2: slotId
 *   3: symbolId
 *   4-5: startPrbu
 *   6-7: numPrbu
 *   8: udCompHdr -> [7:4] compMeth, [3:0] iqWidth (0 means 16)
 *   9: reserved
 * Followed by the IQ payload (raw or BFP, per compMeth).
 */
#define UPLANE_HDR_SIZE 10u
#define RE_PER_PRB      12u

/* iqWidth is carried in 4 bits; O-RAN encodes a width of 16 as 0. */
static uint8_t width_to_wire(uint8_t w) { return (uint8_t)(w & 0x0f); }
static uint8_t width_from_wire(uint8_t w) { return w ? w : 16u; }

/* Size of the IQ payload (excludes the 10-byte header). */
static size_t payload_bytes(uint8_t comp_meth, uint16_t num_prb,
                            uint8_t iq_width)
{
    size_t n = (size_t)num_prb * RE_PER_PRB;
    if (comp_meth == ORAN_COMP_BFP)
        return (size_t)num_prb * bfp_prb_bytes(iq_width);
    return n * 2u * sizeof(int16_t);   /* ORAN_COMP_NONE */
}

int oran_uplane_encode(const oran_uplane_hdr_t *h, const oru_iq16_t *iq,
                       size_t n_samples, uint8_t *buf, size_t len)
{
    if (!h || !iq || !buf)
        return ORU_ERR_PARAM;
    if (h->comp_meth != ORAN_COMP_NONE && h->comp_meth != ORAN_COMP_BFP)
        return ORU_ERR_NOTSUP;

    size_t expect = (size_t)h->num_prb * RE_PER_PRB;
    if (n_samples < expect) {
        LOGE(TAG, "too few samples: have %zu need %zu", n_samples, expect);
        return ORU_ERR_PARAM;
    }

    uint8_t width = (h->comp_meth == ORAN_COMP_BFP) ? h->iq_bitwidth : 16u;
    size_t need = UPLANE_HDR_SIZE + payload_bytes(h->comp_meth, h->num_prb,
                                                  width);
    if (len < need)
        return ORU_ERR_PARAM;

    buf[0] = h->frame_id;
    buf[1] = h->subframe_id;
    buf[2] = h->slot_id;
    buf[3] = h->symbol_id;
    buf[4] = (uint8_t)(h->start_prb >> 8);
    buf[5] = (uint8_t)(h->start_prb & 0xff);
    buf[6] = (uint8_t)(h->num_prb >> 8);
    buf[7] = (uint8_t)(h->num_prb & 0xff);
    buf[8] = (uint8_t)(((h->comp_meth & 0x0f) << 4) | width_to_wire(width));
    buf[9] = 0;

    uint8_t *p = buf + UPLANE_HDR_SIZE;

    if (h->comp_meth == ORAN_COMP_BFP) {
        int rc = bfp_compress(iq, h->num_prb, width, p, need - UPLANE_HDR_SIZE);
        if (rc < 0)
            return rc;
    } else {
        for (size_t k = 0; k < expect; k++) {
            *p++ = (uint8_t)(iq[k].i >> 8);
            *p++ = (uint8_t)(iq[k].i & 0xff);
            *p++ = (uint8_t)(iq[k].q >> 8);
            *p++ = (uint8_t)(iq[k].q & 0xff);
        }
    }
    return (int)need;
}

oru_status_t oran_uplane_decode(const uint8_t *buf, size_t len,
                                oran_uplane_hdr_t *out_hdr,
                                oru_iq16_t *iq, size_t max_samples,
                                size_t *out_n)
{
    if (!buf || !out_hdr || !iq || !out_n)
        return ORU_ERR_PARAM;
    if (len < UPLANE_HDR_SIZE)
        return ORU_ERR_PROTO;

    memset(out_hdr, 0, sizeof(*out_hdr));
    out_hdr->frame_id    = buf[0];
    out_hdr->subframe_id = buf[1];
    out_hdr->slot_id     = buf[2];
    out_hdr->symbol_id   = buf[3];
    out_hdr->start_prb   = (uint16_t)((buf[4] << 8) | buf[5]);
    out_hdr->num_prb     = (uint16_t)((buf[6] << 8) | buf[7]);
    out_hdr->comp_meth   = (uint8_t)((buf[8] >> 4) & 0x0f);
    out_hdr->iq_bitwidth = width_from_wire((uint8_t)(buf[8] & 0x0f));

    if (out_hdr->comp_meth != ORAN_COMP_NONE &&
        out_hdr->comp_meth != ORAN_COMP_BFP)
        return ORU_ERR_NOTSUP;

    size_t n = (size_t)out_hdr->num_prb * RE_PER_PRB;
    size_t need = UPLANE_HDR_SIZE + payload_bytes(out_hdr->comp_meth,
                                                  out_hdr->num_prb,
                                                  out_hdr->iq_bitwidth);
    if (len < need)
        return ORU_ERR_PROTO;
    if (n > max_samples)
        return ORU_ERR_PARAM;

    const uint8_t *p = buf + UPLANE_HDR_SIZE;

    if (out_hdr->comp_meth == ORAN_COMP_BFP) {
        int rc = bfp_decompress(p, len - UPLANE_HDR_SIZE, out_hdr->num_prb,
                                out_hdr->iq_bitwidth, iq, max_samples);
        if (rc < 0)
            return (oru_status_t)rc;
    } else {
        for (size_t k = 0; k < n; k++) {
            iq[k].i = (int16_t)((p[0] << 8) | p[1]);
            iq[k].q = (int16_t)((p[2] << 8) | p[3]);
            p += 4;
        }
    }
    *out_n = n;
    return ORU_OK;
}
