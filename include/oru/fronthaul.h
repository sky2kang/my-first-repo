/* SPDX-License-Identifier: MIT */
/*
 * O-RAN fronthaul: C-plane (scheduling control) and U-plane (IQ data)
 * built on top of eCPRI. See docs/03-oran-fronthaul-7.2x.md.
 */
#ifndef ORU_FRONTHAUL_H
#define ORU_FRONTHAUL_H

#include "oru/types.h"

/* --- C-plane: minimal Section Type 1 descriptor --- */
typedef struct {
    uint8_t  frame_id;
    uint8_t  subframe_id;
    uint8_t  slot_id;
    uint8_t  start_symbol_id;
    uint16_t start_prb;   /* startPrbc */
    uint16_t num_prb;     /* numPrbc   */
    uint16_t beam_id;
} oran_cplane_section_t;

/* --- U-plane: a block of frequency-domain IQ for one symbol --- */
typedef struct {
    uint8_t   frame_id;
    uint8_t   subframe_id;
    uint8_t   slot_id;
    uint8_t   symbol_id;
    uint16_t  start_prb;
    uint16_t  num_prb;          /* 12 REs each */
    uint8_t   iq_bitwidth;      /* udCompHdr bit width (e.g. 9 for BFP) */
} oran_uplane_hdr_t;

/* Fronthaul subsystem lifecycle. */
oru_status_t fronthaul_init(void);
void         fronthaul_shutdown(void);

/* C-plane: encode/decode a Section Type 1 control message payload
 * (after the eCPRI common header). Returns bytes used / oru_status_t. */
int          oran_cplane_encode(const oran_cplane_section_t *s,
                                uint8_t *buf, size_t len);
oru_status_t oran_cplane_decode(const uint8_t *buf, size_t len,
                                oran_cplane_section_t *out);

/* U-plane: pack IQ samples into a U-plane payload, and parse them back.
 * Compression is modeled as a stub (no-op pack) for now; real BFP comes
 * in Phase 1 (see docs/07-roadmap.md). */
int          oran_uplane_encode(const oran_uplane_hdr_t *h,
                                const oru_iq16_t *iq, size_t n_samples,
                                uint8_t *buf, size_t len);
oru_status_t oran_uplane_decode(const uint8_t *buf, size_t len,
                                oran_uplane_hdr_t *out_hdr,
                                oru_iq16_t *iq, size_t max_samples,
                                size_t *out_n);

#endif /* ORU_FRONTHAUL_H */
