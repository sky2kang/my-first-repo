/* SPDX-License-Identifier: MIT */
#include "oru/ecpri.h"
#include "oru/log.h"

#include <string.h>

#define TAG "ecpri"

/*
 * eCPRI common header layout (network byte order), 8-byte form used here:
 *
 *   byte0: [7:4] protocol revision | [3:1] reserved | [0] C (concatenation)
 *   byte1: message type
 *   byte2-3: payload size (bytes, big-endian)
 *   byte4-5: pc_id (big-endian)        \ message-type specific, but common
 *   byte6-7: seq_id (big-endian)       / for IQ data / RT control
 */

int ecpri_hdr_encode(const ecpri_hdr_t *hdr, uint8_t *buf, size_t buf_len)
{
    if (!hdr || !buf)
        return ORU_ERR_PARAM;
    if (buf_len < ECPRI_HEADER_SIZE)
        return ORU_ERR_PARAM;

    buf[0] = (uint8_t)(((hdr->version & 0x0f) << 4) |
                       (hdr->concatenation ? 0x01 : 0x00));
    buf[1] = hdr->msg_type;
    buf[2] = (uint8_t)(hdr->payload_size >> 8);
    buf[3] = (uint8_t)(hdr->payload_size & 0xff);
    buf[4] = (uint8_t)(hdr->pc_id >> 8);
    buf[5] = (uint8_t)(hdr->pc_id & 0xff);
    buf[6] = (uint8_t)(hdr->seq_id >> 8);
    buf[7] = (uint8_t)(hdr->seq_id & 0xff);

    return (int)ECPRI_HEADER_SIZE;
}

oru_status_t ecpri_hdr_decode(const uint8_t *buf, size_t len, ecpri_hdr_t *out)
{
    if (!buf || !out)
        return ORU_ERR_PARAM;
    if (len < ECPRI_HEADER_SIZE)
        return ORU_ERR_PROTO;

    memset(out, 0, sizeof(*out));
    out->version       = (uint8_t)((buf[0] >> 4) & 0x0f);
    out->concatenation = (buf[0] & 0x01) != 0;
    out->msg_type      = buf[1];
    out->payload_size  = (uint16_t)((buf[2] << 8) | buf[3]);
    out->pc_id         = (uint16_t)((buf[4] << 8) | buf[5]);
    out->seq_id        = (uint16_t)((buf[6] << 8) | buf[7]);

    if (out->version != 1)
        LOGW(TAG, "unexpected eCPRI version %u", out->version);

    return ORU_OK;
}
