/* SPDX-License-Identifier: MIT */
/*
 * eCPRI common header (subset) used by the O-RAN fronthaul.
 * Ref: eCPRI Specification V2.0 / O-RAN.WG4.CUS-Plane.
 */
#ifndef ORU_ECPRI_H
#define ORU_ECPRI_H

#include "oru/types.h"

/* eCPRI message types (subset relevant to O-RAN 7.2x). */
typedef enum {
    ECPRI_MSG_IQ_DATA        = 0x00, /* U-plane                       */
    ECPRI_MSG_RT_CONTROL     = 0x02, /* C-plane (real-time control)   */
    ECPRI_MSG_ONE_WAY_DELAY  = 0x05, /* S-plane delay measurement     */
} ecpri_msg_type_t;

#define ECPRI_HEADER_SIZE 8u  /* common header bytes we serialize */

/* Decoded view of an eCPRI common header. */
typedef struct {
    uint8_t  version;       /* protocol revision (1)                 */
    bool     concatenation; /* C bit: more eCPRI msgs follow         */
    uint8_t  msg_type;      /* ecpri_msg_type_t                      */
    uint16_t payload_size;  /* bytes of payload after the header     */
    uint16_t pc_id;         /* physical channel / eAxC id            */
    uint16_t seq_id;        /* sequence id                           */
} ecpri_hdr_t;

/* Serialize hdr into buf (must be >= ECPRI_HEADER_SIZE). Returns bytes
 * written, or negative oru_status_t on error. */
int ecpri_hdr_encode(const ecpri_hdr_t *hdr, uint8_t *buf, size_t buf_len);

/* Parse an eCPRI common header from buf. Returns ORU_OK on success. */
oru_status_t ecpri_hdr_decode(const uint8_t *buf, size_t len,
                              ecpri_hdr_t *out);

#endif /* ORU_ECPRI_H */
