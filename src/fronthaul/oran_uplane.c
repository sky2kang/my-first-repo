/* SPDX-License-Identifier: MIT */
/*
 * O-RAN U-plane: frequency-domain IQ transport.
 *
 * Compression is currently a no-op "uncompressed 16-bit" model so the data
 * path is exercisable end-to-end on a host. Real BFP (Block Floating Point)
 * compression lands in Phase 1 (docs/07-roadmap.md). The header byte layout
 * below is a simplified subset of the O-RAN U-plane section.
 */
#include "oru/fronthaul.h"
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
 *   8: iq bit width (udCompHdr, lower nibble = bitwidth)
 *   9: reserved
 * Followed by IQ payload: numPrbu * 12 REs * 2 * sizeof(int16) (uncompressed).
 */
#define UPLANE_HDR_SIZE 10u
#define RE_PER_PRB      12u

int oran_uplane_encode(const oran_uplane_hdr_t *h, const oru_iq16_t *iq,
                       size_t n_samples, uint8_t *buf, size_t len)
{
    if (!h || !iq || !buf)
        return ORU_ERR_PARAM;

    size_t expect = (size_t)h->num_prb * RE_PER_PRB;
    if (n_samples < expect) {
        LOGE(TAG, "too few samples: have %zu need %zu", n_samples, expect);
        return ORU_ERR_PARAM;
    }

    size_t need = UPLANE_HDR_SIZE + expect * 2 * sizeof(int16_t);
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
    buf[8] = (uint8_t)(h->iq_bitwidth & 0x0f);
    buf[9] = 0;

    uint8_t *p = buf + UPLANE_HDR_SIZE;
    for (size_t k = 0; k < expect; k++) {
        *p++ = (uint8_t)(iq[k].i >> 8);
        *p++ = (uint8_t)(iq[k].i & 0xff);
        *p++ = (uint8_t)(iq[k].q >> 8);
        *p++ = (uint8_t)(iq[k].q & 0xff);
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
    out_hdr->iq_bitwidth = (uint8_t)(buf[8] & 0x0f);

    size_t n = (size_t)out_hdr->num_prb * RE_PER_PRB;
    size_t need = UPLANE_HDR_SIZE + n * 2 * sizeof(int16_t);
    if (len < need)
        return ORU_ERR_PROTO;
    if (n > max_samples)
        return ORU_ERR_PARAM;

    const uint8_t *p = buf + UPLANE_HDR_SIZE;
    for (size_t k = 0; k < n; k++) {
        iq[k].i = (int16_t)((p[0] << 8) | p[1]);
        iq[k].q = (int16_t)((p[2] << 8) | p[3]);
        p += 4;
    }
    *out_n = n;
    return ORU_OK;
}
