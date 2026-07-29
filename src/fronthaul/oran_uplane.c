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
#include "oru/mulaw.h"
#include "oru/modcomp.h"
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
    switch (comp_meth) {
    case ORAN_COMP_BFP:        return (size_t)num_prb * bfp_prb_bytes(iq_width);
    case ORAN_COMP_MULAW:      return mulaw_bytes(n, iq_width);
    case ORAN_COMP_MODULATION: return (size_t)num_prb * modcomp_prb_bytes(iq_width);
    default:                   return n * 2u * sizeof(int16_t);  /* NONE */
    }
}

/* Pack `num_prb` PRBs of IQ into `dst` per comp_meth. Returns bytes used
 * or a negative oru_status_t. Shared by the single- and multi-section
 * encoders. */
static int pack_iq(uint8_t comp_meth, uint8_t iq_width, uint16_t num_prb,
                   const oru_iq16_t *iq, uint8_t *dst, size_t dst_len)
{
    size_t n = (size_t)num_prb * RE_PER_PRB;
    size_t need = payload_bytes(comp_meth, num_prb, iq_width);
    if (dst_len < need)
        return ORU_ERR_PARAM;

    switch (comp_meth) {
    case ORAN_COMP_BFP:
        return bfp_compress(iq, num_prb, iq_width, dst, dst_len);
    case ORAN_COMP_MULAW:
        return mulaw_compress(iq, n, iq_width, dst, dst_len);
    case ORAN_COMP_MODULATION:
        return modcomp_compress(iq, num_prb, iq_width, dst, dst_len);
    default: {  /* ORAN_COMP_NONE */
        uint8_t *p = dst;
        for (size_t k = 0; k < n; k++) {
            *p++ = (uint8_t)(iq[k].i >> 8);
            *p++ = (uint8_t)(iq[k].i & 0xff);
            *p++ = (uint8_t)(iq[k].q >> 8);
            *p++ = (uint8_t)(iq[k].q & 0xff);
        }
        return (int)need;
    }
    }
}

/* Inverse of pack_iq(). Returns complex sample count or negative status. */
static int unpack_iq(uint8_t comp_meth, uint8_t iq_width, uint16_t num_prb,
                     const uint8_t *src, size_t src_len,
                     oru_iq16_t *iq, size_t max_samples)
{
    size_t n = (size_t)num_prb * RE_PER_PRB;
    size_t need = payload_bytes(comp_meth, num_prb, iq_width);
    if (src_len < need)
        return ORU_ERR_PROTO;
    if (n > max_samples)
        return ORU_ERR_PARAM;

    switch (comp_meth) {
    case ORAN_COMP_BFP:
        return bfp_decompress(src, src_len, num_prb, iq_width, iq,
                              max_samples);
    case ORAN_COMP_MULAW:
        return mulaw_decompress(src, src_len, n, iq_width, iq, max_samples);
    case ORAN_COMP_MODULATION:
        return modcomp_decompress(src, src_len, num_prb, iq_width, iq,
                                  max_samples);
    default: {  /* ORAN_COMP_NONE */
        const uint8_t *p = src;
        for (size_t k = 0; k < n; k++) {
            iq[k].i = (int16_t)((p[0] << 8) | p[1]);
            iq[k].q = (int16_t)((p[2] << 8) | p[3]);
            p += 4;
        }
        return (int)n;
    }
    }
}

static int comp_meth_ok(uint8_t m)
{
    return m == ORAN_COMP_NONE || m == ORAN_COMP_BFP ||
           m == ORAN_COMP_MULAW || m == ORAN_COMP_MODULATION;
}

/* The wire iqWidth field is 4 bits; methods that use a per-component width
 * (BFP, µ-law) must fit 4..15 there (16 is encoded as 0). NONE ignores it. */
static uint8_t effective_width(uint8_t comp_meth, uint8_t iq_width)
{
    return (comp_meth == ORAN_COMP_NONE) ? 16u : iq_width;
}

int oran_uplane_encode(const oran_uplane_hdr_t *h, const oru_iq16_t *iq,
                       size_t n_samples, uint8_t *buf, size_t len)
{
    if (!h || !iq || !buf)
        return ORU_ERR_PARAM;
    if (!comp_meth_ok(h->comp_meth))
        return ORU_ERR_NOTSUP;

    size_t expect = (size_t)h->num_prb * RE_PER_PRB;
    if (n_samples < expect) {
        LOGE(TAG, "too few samples: have %zu need %zu", n_samples, expect);
        return ORU_ERR_PARAM;
    }

    uint8_t width = effective_width(h->comp_meth, h->iq_bitwidth);
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

    int rc = pack_iq(h->comp_meth, width, h->num_prb, iq,
                     buf + UPLANE_HDR_SIZE, len - UPLANE_HDR_SIZE);
    if (rc < 0)
        return rc;
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

    if (!comp_meth_ok(out_hdr->comp_meth))
        return ORU_ERR_NOTSUP;

    size_t n = (size_t)out_hdr->num_prb * RE_PER_PRB;
    size_t need = UPLANE_HDR_SIZE + payload_bytes(out_hdr->comp_meth,
                                                  out_hdr->num_prb,
                                                  out_hdr->iq_bitwidth);
    if (len < need)
        return ORU_ERR_PROTO;
    if (n > max_samples)
        return ORU_ERR_PARAM;

    int rc = unpack_iq(out_hdr->comp_meth, out_hdr->iq_bitwidth,
                       out_hdr->num_prb, buf + UPLANE_HDR_SIZE,
                       len - UPLANE_HDR_SIZE, iq, max_samples);
    if (rc < 0)
        return (oru_status_t)rc;
    *out_n = n;
    return ORU_OK;
}

/* --- Multi-section message (radio-app header + N sections) --------------- */

/* The real O-RAN radio-app header bit-packs frame/subframe/slot/symbol
 * tightly. To stay unambiguous and teaching-friendly we use a flat layout.
 *
 * Radio-application header (8 bytes):
 *   0: [7] dataDirection | [6:4] payloadVersion | [3:0] filterIndex
 *   1: frameId
 *   2: subframeId
 *   3: slotId
 *   4: startSymbolId
 *   5: numberOfSections
 *   6: reserved
 *   7: reserved
 * Per-section header (8 bytes):
 *   0-1: sectionId
 *   2-3: startPrbu
 *   4-5: numPrbu
 *   6:   udCompHdr -> [7:4] compMeth, [3:0] iqWidth (0 means 16)
 *   7:   reserved
 * ... followed immediately by that section's IQ payload.
 */
#define RADIO_APP_HDR_SIZE 8u
#define USEC_HDR_SIZE      8u

int oran_uplane_msg_encode(const oran_radio_app_hdr_t *app,
                           const oran_uplane_section_t *sections,
                           size_t n_sections,
                           uint8_t *buf, size_t len)
{
    if (!app || !sections || !buf)
        return ORU_ERR_PARAM;
    if (n_sections == 0 || n_sections > ORAN_MAX_SECTIONS)
        return ORU_ERR_PARAM;
    if (len < RADIO_APP_HDR_SIZE)
        return ORU_ERR_PARAM;

    buf[0] = (uint8_t)(((app->data_direction & 0x01) << 7) |
                       ((app->payload_version & 0x07) << 4) |
                       (app->filter_index & 0x0f));
    buf[1] = app->frame_id;
    buf[2] = app->subframe_id;
    buf[3] = app->slot_id;
    buf[4] = app->start_symbol_id;
    buf[5] = (uint8_t)n_sections;
    buf[6] = 0;
    buf[7] = 0;

    size_t off = RADIO_APP_HDR_SIZE;
    for (size_t s = 0; s < n_sections; s++) {
        const oran_uplane_section_t *sec = &sections[s];
        if (!comp_meth_ok(sec->hdr.comp_meth))
            return ORU_ERR_NOTSUP;
        if (!sec->iq)
            return ORU_ERR_PARAM;
        size_t expect = (size_t)sec->hdr.num_prb * RE_PER_PRB;
        if (sec->n_samples < expect)
            return ORU_ERR_PARAM;

        uint8_t width = effective_width(sec->hdr.comp_meth,
                                        sec->hdr.iq_bitwidth);
        size_t pay = payload_bytes(sec->hdr.comp_meth, sec->hdr.num_prb,
                                   width);
        if (off + USEC_HDR_SIZE + pay > len)
            return ORU_ERR_PARAM;

        uint8_t *h = buf + off;
        h[0] = (uint8_t)(sec->hdr.section_id >> 8);
        h[1] = (uint8_t)(sec->hdr.section_id & 0xff);
        h[2] = (uint8_t)(sec->hdr.start_prb >> 8);
        h[3] = (uint8_t)(sec->hdr.start_prb & 0xff);
        h[4] = (uint8_t)(sec->hdr.num_prb >> 8);
        h[5] = (uint8_t)(sec->hdr.num_prb & 0xff);
        h[6] = (uint8_t)(((sec->hdr.comp_meth & 0x0f) << 4) |
                         width_to_wire(width));
        h[7] = 0;
        off += USEC_HDR_SIZE;

        int rc = pack_iq(sec->hdr.comp_meth, width, sec->hdr.num_prb,
                         sec->iq, buf + off, len - off);
        if (rc < 0)
            return rc;
        off += (size_t)rc;
    }

    LOGT(TAG, "encoded U-plane msg: %zu sections, %zu bytes", n_sections, off);
    return (int)off;
}

oru_status_t oran_uplane_msg_decode(const uint8_t *buf, size_t len,
                                    oran_uplane_msg_t *out,
                                    oru_iq16_t *iq, size_t max_samples)
{
    if (!buf || !out || !iq)
        return ORU_ERR_PARAM;
    if (len < RADIO_APP_HDR_SIZE)
        return ORU_ERR_PROTO;

    memset(out, 0, sizeof(*out));
    out->app.data_direction  = (uint8_t)((buf[0] >> 7) & 0x01);
    out->app.payload_version = (uint8_t)((buf[0] >> 4) & 0x07);
    out->app.filter_index    = (uint8_t)(buf[0] & 0x0f);
    out->app.frame_id        = buf[1];
    out->app.subframe_id     = buf[2];
    out->app.slot_id         = buf[3];
    out->app.start_symbol_id = buf[4];

    size_t n_sections = buf[5];
    if (n_sections == 0 || n_sections > ORAN_MAX_SECTIONS)
        return ORU_ERR_PROTO;

    size_t off = RADIO_APP_HDR_SIZE;
    size_t total_samples = 0;

    for (size_t s = 0; s < n_sections; s++) {
        if (off + USEC_HDR_SIZE > len)
            return ORU_ERR_PROTO;

        const uint8_t *h = buf + off;
        oran_uplane_section_hdr_t *sh = &out->sec_hdrs[s];
        sh->section_id  = (uint16_t)((h[0] << 8) | h[1]);
        sh->start_prb   = (uint16_t)((h[2] << 8) | h[3]);
        sh->num_prb     = (uint16_t)((h[4] << 8) | h[5]);
        sh->comp_meth   = (uint8_t)((h[6] >> 4) & 0x0f);
        sh->iq_bitwidth = width_from_wire((uint8_t)(h[6] & 0x0f));
        if (!comp_meth_ok(sh->comp_meth))
            return ORU_ERR_NOTSUP;
        off += USEC_HDR_SIZE;

        size_t n = (size_t)sh->num_prb * RE_PER_PRB;
        if (total_samples + n > max_samples)
            return ORU_ERR_PARAM;

        int rc = unpack_iq(sh->comp_meth, sh->iq_bitwidth, sh->num_prb,
                           buf + off, len - off,
                           iq + total_samples, max_samples - total_samples);
        if (rc < 0)
            return (oru_status_t)rc;

        out->sec_offsets[s] = total_samples;
        total_samples += n;
        off += payload_bytes(sh->comp_meth, sh->num_prb, sh->iq_bitwidth);
    }

    out->n_sections = n_sections;
    out->n_samples  = total_samples;
    return ORU_OK;
}
