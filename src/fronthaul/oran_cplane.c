/* SPDX-License-Identifier: MIT */
/*
 * O-RAN C-plane, Section Type 1 (most DL/UL data), minimal encoding.
 * This is a teaching-grade subset of O-RAN.WG4.CUS-Plane, enough to carry
 * frame/slot/symbol timing and a PRB range + beam. Real implementations
 * carry the full radio-app header, compression headers, and multiple
 * sections per message.
 */
#include "oru/fronthaul.h"
#include "oru/log.h"

#include <string.h>

#define TAG "cplane"

/* Wire layout (12 bytes), big-endian where multi-byte:
 *   0: frameId
 *   1: subframeId
 *   2: slotId
 *   3: startSymbolId
 *   4-5: startPrbc
 *   6-7: numPrbc
 *   8-9: beamId
 *   10-11: reserved
 */
#define CPLANE_SEC1_SIZE 12u

int oran_cplane_encode(const oran_cplane_section_t *s, uint8_t *buf, size_t len)
{
    if (!s || !buf)
        return ORU_ERR_PARAM;
    if (len < CPLANE_SEC1_SIZE)
        return ORU_ERR_PARAM;

    buf[0]  = s->frame_id;
    buf[1]  = s->subframe_id;
    buf[2]  = s->slot_id;
    buf[3]  = s->start_symbol_id;
    buf[4]  = (uint8_t)(s->start_prb >> 8);
    buf[5]  = (uint8_t)(s->start_prb & 0xff);
    buf[6]  = (uint8_t)(s->num_prb >> 8);
    buf[7]  = (uint8_t)(s->num_prb & 0xff);
    buf[8]  = (uint8_t)(s->beam_id >> 8);
    buf[9]  = (uint8_t)(s->beam_id & 0xff);
    buf[10] = 0;
    buf[11] = 0;

    return (int)CPLANE_SEC1_SIZE;
}

oru_status_t oran_cplane_decode(const uint8_t *buf, size_t len,
                                oran_cplane_section_t *out)
{
    if (!buf || !out)
        return ORU_ERR_PARAM;
    if (len < CPLANE_SEC1_SIZE)
        return ORU_ERR_PROTO;

    memset(out, 0, sizeof(*out));
    out->frame_id        = buf[0];
    out->subframe_id     = buf[1];
    out->slot_id         = buf[2];
    out->start_symbol_id = buf[3];
    out->start_prb       = (uint16_t)((buf[4] << 8) | buf[5]);
    out->num_prb         = (uint16_t)((buf[6] << 8) | buf[7]);
    out->beam_id         = (uint16_t)((buf[8] << 8) | buf[9]);

    LOGT(TAG, "decoded Sec1 f=%u sf=%u sl=%u sym=%u prb=[%u..+%u] beam=%u",
         out->frame_id, out->subframe_id, out->slot_id,
         out->start_symbol_id, out->start_prb, out->num_prb, out->beam_id);
    return ORU_OK;
}

/* --- Section Type 3 (PRACH / mixed-numerology) ---------------------------
 * Wire layout (20 bytes), big-endian where multi-byte. Section Type 3 adds
 * the timeOffset / frameStructure / cpLength / freqOffset fields that
 * Section Type 1 does not carry.
 *   0:     frameId
 *   1:     subframeId
 *   2:     slotId
 *   3:     startSymbolId
 *   4-5:   startPrbc
 *   6-7:   numPrbc
 *   8-9:   beamId
 *   10-11: timeOffset
 *   12:    frameStructure ([7:4] FFT size index, [3:0] numerology mu)
 *   13-14: cpLength
 *   15-17: freqOffset (24-bit signed, two's complement, big-endian)
 *   18-19: reserved
 */
#define CPLANE_SEC3_SIZE 20u
#define FREQ_OFFSET_MIN  (-(1 << 23))
#define FREQ_OFFSET_MAX  ((1 << 23) - 1)

int oran_cplane3_encode(const oran_cplane_section3_t *s, uint8_t *buf,
                        size_t len)
{
    if (!s || !buf)
        return ORU_ERR_PARAM;
    if (len < CPLANE_SEC3_SIZE)
        return ORU_ERR_PARAM;
    if (s->freq_offset < FREQ_OFFSET_MIN || s->freq_offset > FREQ_OFFSET_MAX)
        return ORU_ERR_PARAM;

    buf[0]  = s->frame_id;
    buf[1]  = s->subframe_id;
    buf[2]  = s->slot_id;
    buf[3]  = s->start_symbol_id;
    buf[4]  = (uint8_t)(s->start_prb >> 8);
    buf[5]  = (uint8_t)(s->start_prb & 0xff);
    buf[6]  = (uint8_t)(s->num_prb >> 8);
    buf[7]  = (uint8_t)(s->num_prb & 0xff);
    buf[8]  = (uint8_t)(s->beam_id >> 8);
    buf[9]  = (uint8_t)(s->beam_id & 0xff);
    buf[10] = (uint8_t)(s->time_offset >> 8);
    buf[11] = (uint8_t)(s->time_offset & 0xff);
    buf[12] = s->frame_structure;
    buf[13] = (uint8_t)(s->cp_length >> 8);
    buf[14] = (uint8_t)(s->cp_length & 0xff);

    uint32_t fo = (uint32_t)s->freq_offset & 0x00ffffffu;  /* 24-bit */
    buf[15] = (uint8_t)(fo >> 16);
    buf[16] = (uint8_t)(fo >> 8);
    buf[17] = (uint8_t)(fo & 0xff);
    buf[18] = 0;
    buf[19] = 0;

    return (int)CPLANE_SEC3_SIZE;
}

oru_status_t oran_cplane3_decode(const uint8_t *buf, size_t len,
                                 oran_cplane_section3_t *out)
{
    if (!buf || !out)
        return ORU_ERR_PARAM;
    if (len < CPLANE_SEC3_SIZE)
        return ORU_ERR_PROTO;

    memset(out, 0, sizeof(*out));
    out->frame_id        = buf[0];
    out->subframe_id     = buf[1];
    out->slot_id         = buf[2];
    out->start_symbol_id = buf[3];
    out->start_prb       = (uint16_t)((buf[4] << 8) | buf[5]);
    out->num_prb         = (uint16_t)((buf[6] << 8) | buf[7]);
    out->beam_id         = (uint16_t)((buf[8] << 8) | buf[9]);
    out->time_offset     = (uint16_t)((buf[10] << 8) | buf[11]);
    out->frame_structure = buf[12];
    out->cp_length       = (uint16_t)((buf[13] << 8) | buf[14]);

    uint32_t fo = ((uint32_t)buf[15] << 16) |
                  ((uint32_t)buf[16] << 8)  |
                  (uint32_t)buf[17];
    if (fo & 0x00800000u)            /* sign-extend 24-bit to 32-bit */
        fo |= 0xff000000u;
    out->freq_offset = (int32_t)fo;

    LOGT(TAG, "decoded Sec3 f=%u sl=%u sym=%u prb=[%u..+%u] mu=%u "
              "toff=%u cp=%u foff=%d",
         out->frame_id, out->slot_id, out->start_symbol_id,
         out->start_prb, out->num_prb, (unsigned)(out->frame_structure & 0x0f),
         out->time_offset, out->cp_length, out->freq_offset);
    return ORU_OK;
}
