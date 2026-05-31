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
