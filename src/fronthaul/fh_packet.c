/* SPDX-License-Identifier: MIT */
#include "oru/fh_packet.h"
#include "oru/log.h"

#include <string.h>

#define TAG "fh_packet"

const char *fh_pkt_kind_str(fh_pkt_kind_t k)
{
    switch (k) {
    case FH_PKT_CPLANE_S1: return "C-plane/S1";
    case FH_PKT_CPLANE_S3: return "C-plane/S3";
    case FH_PKT_UPLANE:    return "U-plane";
    default:               return "?";
    }
}

/* --- Sender ------------------------------------------------------------- */

void fh_tx_init(fh_tx_t *tx)
{
    if (tx)
        memset(tx, 0, sizeof(*tx));
}

/* Find (or create) the seq slot for an eAxC. Returns index or -1 if full. */
static int tx_slot(fh_tx_t *tx, uint16_t eaxc)
{
    for (size_t i = 0; i < tx->n; i++)
        if (tx->eaxc[i] == eaxc)
            return (int)i;
    if (tx->n >= FH_MAX_EAXC)
        return -1;
    tx->eaxc[tx->n] = eaxc;
    tx->next_seq[tx->n] = 0;
    return (int)tx->n++;
}

/* Common prologue: reserve eCPRI header space, return payload pointer. */
static int begin_packet(fh_tx_t *tx, uint16_t eaxc, uint8_t **payload,
                        size_t *avail, uint8_t *buf, size_t len,
                        uint8_t *seq_out)
{
    if (!tx || !buf)
        return ORU_ERR_PARAM;
    if (len < ECPRI_HEADER_SIZE)
        return ORU_ERR_PARAM;
    int slot = tx_slot(tx, eaxc);
    if (slot < 0)
        return ORU_ERR;
    *seq_out = tx->next_seq[slot];
    *payload = buf + ECPRI_HEADER_SIZE;
    *avail = len - ECPRI_HEADER_SIZE;
    return slot;
}

/* Common epilogue: write the eCPRI header in front of a payload of
 * `pay_len` bytes and bump the eAxC sequence. Returns total packet bytes. */
static int finish_packet(fh_tx_t *tx, int slot, uint16_t eaxc,
                         uint8_t msg_type, size_t pay_len, uint8_t seq,
                         uint8_t *buf)
{
    ecpri_hdr_t h = {
        .version = 1, .concatenation = false, .msg_type = msg_type,
        .payload_size = (uint16_t)pay_len, .pc_id = eaxc, .seq_id = seq,
    };
    int hn = ecpri_hdr_encode(&h, buf, ECPRI_HEADER_SIZE);
    if (hn < 0)
        return hn;
    tx->next_seq[slot]++;
    return (int)(ECPRI_HEADER_SIZE + pay_len);
}

int fh_build_cplane_s1(fh_tx_t *tx, uint16_t eaxc,
                       const oran_cplane_section_t *s,
                       uint8_t *buf, size_t len)
{
    if (!s)
        return ORU_ERR_PARAM;
    uint8_t *pay; size_t avail; uint8_t seq;
    int slot = begin_packet(tx, eaxc, &pay, &avail, buf, len, &seq);
    if (slot < 0)
        return slot;
    int pn = oran_cplane_encode(s, pay, avail);
    if (pn < 0)
        return pn;
    return finish_packet(tx, slot, eaxc, ECPRI_MSG_RT_CONTROL,
                         (size_t)pn, seq, buf);
}

int fh_build_cplane_s3(fh_tx_t *tx, uint16_t eaxc,
                       const oran_cplane_section3_t *s,
                       uint8_t *buf, size_t len)
{
    if (!s)
        return ORU_ERR_PARAM;
    uint8_t *pay; size_t avail; uint8_t seq;
    int slot = begin_packet(tx, eaxc, &pay, &avail, buf, len, &seq);
    if (slot < 0)
        return slot;
    int pn = oran_cplane3_encode(s, pay, avail);
    if (pn < 0)
        return pn;
    return finish_packet(tx, slot, eaxc, ECPRI_MSG_RT_CONTROL,
                         (size_t)pn, seq, buf);
}

int fh_build_uplane(fh_tx_t *tx, uint16_t eaxc,
                    const oran_radio_app_hdr_t *app,
                    const oran_uplane_section_t *sections, size_t n_sections,
                    uint8_t *buf, size_t len)
{
    if (!app || !sections)
        return ORU_ERR_PARAM;
    uint8_t *pay; size_t avail; uint8_t seq;
    int slot = begin_packet(tx, eaxc, &pay, &avail, buf, len, &seq);
    if (slot < 0)
        return slot;
    int pn = oran_uplane_msg_encode(app, sections, n_sections, pay, avail);
    if (pn < 0)
        return pn;
    return finish_packet(tx, slot, eaxc, ECPRI_MSG_IQ_DATA,
                         (size_t)pn, seq, buf);
}

/* --- Receiver ----------------------------------------------------------- */

void fh_rx_init(fh_rx_t *rx)
{
    if (rx)
        memset(rx, 0, sizeof(*rx));
}

static int rx_slot(fh_rx_t *rx, uint16_t eaxc)
{
    for (size_t i = 0; i < rx->n; i++)
        if (rx->eaxc[i] == eaxc)
            return (int)i;
    if (rx->n >= FH_MAX_EAXC)
        return -1;
    rx->eaxc[rx->n] = eaxc;
    rx->exp_seq[rx->n] = 0;
    rx->seen[rx->n] = false;
    return (int)rx->n++;
}

/* Validate seq_id for the eAxC; update expected seq to (seq+1). Returns
 * true if the packet's seq matched the expected value. */
static bool check_seq(fh_rx_t *rx, uint16_t eaxc, uint8_t seq)
{
    int slot = rx_slot(rx, eaxc);
    if (slot < 0)
        return false;

    bool ok;
    if (!rx->seen[slot]) {
        /* First packet on this flow defines the baseline; always accepted. */
        ok = true;
    } else {
        ok = (seq == rx->exp_seq[slot]);
        if (!ok)
            rx->seq_gaps++;
    }
    rx->seen[slot] = true;
    rx->exp_seq[slot] = (uint8_t)(seq + 1);
    return ok;
}

oru_status_t fh_parse_cplane(fh_rx_t *rx, const uint8_t *buf, size_t len,
                             fh_pkt_t *out)
{
    if (!rx || !buf || !out)
        return ORU_ERR_PARAM;

    oru_status_t rc = ecpri_hdr_decode(buf, len, &out->ecpri);
    if (rc != ORU_OK)
        return rc;
    if (out->ecpri.msg_type != ECPRI_MSG_RT_CONTROL)
        return ORU_ERR_PROTO;

    const uint8_t *pay = buf + ECPRI_HEADER_SIZE;
    size_t pay_len = len - ECPRI_HEADER_SIZE;
    if (pay_len < out->ecpri.payload_size)
        return ORU_ERR_PROTO;
    pay_len = out->ecpri.payload_size;

    /* Dispatch by payload length: Section Type 1 is 12 bytes, Type 3 is 20.
     * (The encoders produce exactly these fixed sizes.) */
    if (pay_len == 12u) {
        rc = oran_cplane_decode(pay, pay_len, &out->cplane.s1);
        out->kind = FH_PKT_CPLANE_S1;
    } else if (pay_len == 20u) {
        rc = oran_cplane3_decode(pay, pay_len, &out->cplane.s3);
        out->kind = FH_PKT_CPLANE_S3;
    } else {
        return ORU_ERR_PROTO;
    }
    if (rc != ORU_OK)
        return rc;

    out->seq_ok = check_seq(rx, out->ecpri.pc_id, out->ecpri.seq_id);
    rx->pkts_ok++;
    return ORU_OK;
}

oru_status_t fh_parse_uplane(fh_rx_t *rx, const uint8_t *buf, size_t len,
                             fh_pkt_t *out, oran_uplane_msg_t *msg,
                             oru_iq16_t *iq, size_t max_samples)
{
    if (!rx || !buf || !out || !msg || !iq)
        return ORU_ERR_PARAM;

    oru_status_t rc = ecpri_hdr_decode(buf, len, &out->ecpri);
    if (rc != ORU_OK)
        return rc;
    if (out->ecpri.msg_type != ECPRI_MSG_IQ_DATA)
        return ORU_ERR_PROTO;

    const uint8_t *pay = buf + ECPRI_HEADER_SIZE;
    size_t pay_len = len - ECPRI_HEADER_SIZE;
    if (pay_len < out->ecpri.payload_size)
        return ORU_ERR_PROTO;
    pay_len = out->ecpri.payload_size;

    rc = oran_uplane_msg_decode(pay, pay_len, msg, iq, max_samples);
    if (rc != ORU_OK)
        return rc;

    out->kind = FH_PKT_UPLANE;
    out->seq_ok = check_seq(rx, out->ecpri.pc_id, out->ecpri.seq_id);
    rx->pkts_ok++;
    return ORU_OK;
}
