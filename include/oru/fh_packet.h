/* SPDX-License-Identifier: MIT */
/*
 * Fronthaul packet assembly/parsing: the top layer that joins the eCPRI
 * common header (oru/ecpri.h) to a C-plane or U-plane payload
 * (oru/fronthaul.h) to form one complete on-wire fronthaul packet.
 *
 *   +------------------+-------------------------------------------+
 *   | eCPRI header (8) | payload: C-plane section  OR  U-plane msg |
 *   +------------------+-------------------------------------------+
 *
 * It also owns per-eAxC sequence-id state: each flow (identified by its
 * eAxC / pc_id) carries a monotonically increasing seq_id so the receiver
 * can detect loss/reordering. A small sender keeps the next seq_id per
 * eAxC; the receiver validates the arriving seq_id against what it expects.
 *
 * This gives a single, testable "build a packet / parse a packet" surface
 * over everything assembled so far, without any real network I/O.
 */
#ifndef ORU_FH_PACKET_H
#define ORU_FH_PACKET_H

#include "oru/types.h"
#include "oru/ecpri.h"
#include "oru/fronthaul.h"

/* Decoded classification of a received fronthaul packet. */
typedef enum {
    FH_PKT_CPLANE_S1 = 0,  /* C-plane Section Type 1 */
    FH_PKT_CPLANE_S3,      /* C-plane Section Type 3 */
    FH_PKT_UPLANE,         /* U-plane (multi-section) */
} fh_pkt_kind_t;

/* --- Sender: tracks the next seq_id per eAxC --- */
#define FH_MAX_EAXC 16u

typedef struct {
    uint16_t eaxc[FH_MAX_EAXC];      /* known eAxC ids                 */
    uint8_t  next_seq[FH_MAX_EAXC];  /* next seq_id to send for each   */
    size_t   n;
} fh_tx_t;

void fh_tx_init(fh_tx_t *tx);

/* Build a C-plane Section Type 1 packet for `eaxc`. Returns total bytes
 * (eCPRI header + payload), or a negative oru_status_t. */
int fh_build_cplane_s1(fh_tx_t *tx, uint16_t eaxc,
                       const oran_cplane_section_t *s,
                       uint8_t *buf, size_t len);

/* Build a C-plane Section Type 3 packet for `eaxc`. */
int fh_build_cplane_s3(fh_tx_t *tx, uint16_t eaxc,
                       const oran_cplane_section3_t *s,
                       uint8_t *buf, size_t len);

/* Build a U-plane packet (radio-app header + sections) for `eaxc`. */
int fh_build_uplane(fh_tx_t *tx, uint16_t eaxc,
                    const oran_radio_app_hdr_t *app,
                    const oran_uplane_section_t *sections, size_t n_sections,
                    uint8_t *buf, size_t len);

/* --- Receiver: validates seq_id per eAxC --- */
typedef struct {
    uint16_t eaxc[FH_MAX_EAXC];
    uint8_t  exp_seq[FH_MAX_EAXC];   /* next expected seq_id           */
    bool     seen[FH_MAX_EAXC];
    size_t   n;
    uint64_t pkts_ok;
    uint64_t seq_gaps;               /* count of seq_id discontinuities */
} fh_rx_t;

void fh_rx_init(fh_rx_t *rx);

/* Parsed view of a received packet (decoded eCPRI + payload). */
typedef struct {
    ecpri_hdr_t   ecpri;
    fh_pkt_kind_t kind;
    bool          seq_ok;     /* false if seq_id != expected for eAxC  */
    union {
        oran_cplane_section_t  s1;
        oran_cplane_section3_t s3;
    } cplane;
    /* For U-plane the caller supplies storage via fh_parse_uplane(). */
} fh_pkt_t;

/*
 * Parse the eCPRI header and a C-plane payload, updating per-eAxC sequence
 * tracking. Use for packets you expect to be C-plane; returns ORU_ERR_PROTO
 * if the eCPRI message type is not real-time control.
 */
oru_status_t fh_parse_cplane(fh_rx_t *rx, const uint8_t *buf, size_t len,
                             fh_pkt_t *out);

/*
 * Parse a U-plane packet. `msg`, `iq`, and `max_samples` receive the decoded
 * multi-section data (see oran_uplane_msg_decode). Updates sequence tracking.
 */
oru_status_t fh_parse_uplane(fh_rx_t *rx, const uint8_t *buf, size_t len,
                             fh_pkt_t *out, oran_uplane_msg_t *msg,
                             oru_iq16_t *iq, size_t max_samples);

const char *fh_pkt_kind_str(fh_pkt_kind_t k);

#endif /* ORU_FH_PACKET_H */
