/* SPDX-License-Identifier: MIT */
/* Tests for the fronthaul packet assembly/parsing layer (oru/fh_packet.h). */
#include "oru/fh_packet.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define EAXC 0x0042

static void test_cplane_s1_roundtrip(void)
{
    fh_tx_t tx; fh_tx_init(&tx);
    fh_rx_t rx; fh_rx_init(&rx);

    oran_cplane_section_t s = {
        .frame_id = 5, .subframe_id = 2, .slot_id = 1, .start_symbol_id = 0,
        .start_prb = 0, .num_prb = 100, .beam_id = 7,
    };
    uint8_t pkt[256];
    int n = fh_build_cplane_s1(&tx, EAXC, &s, pkt, sizeof(pkt));
    assert(n == (int)(ECPRI_HEADER_SIZE + 12u));

    fh_pkt_t p;
    assert(fh_parse_cplane(&rx, pkt, (size_t)n, &p) == ORU_OK);
    assert(p.kind == FH_PKT_CPLANE_S1);
    assert(p.ecpri.pc_id == EAXC);
    assert(p.ecpri.msg_type == ECPRI_MSG_RT_CONTROL);
    assert(p.seq_ok);
    assert(p.cplane.s1.num_prb == 100);
    assert(p.cplane.s1.beam_id == 7);
}

static void test_cplane_s3_roundtrip(void)
{
    fh_tx_t tx; fh_tx_init(&tx);
    fh_rx_t rx; fh_rx_init(&rx);

    oran_cplane_section3_t s = {
        .frame_id = 9, .slot_id = 0, .start_symbol_id = 0,
        .start_prb = 6, .num_prb = 12, .time_offset = 4096,
        .frame_structure = 0x31, .cp_length = 288, .freq_offset = -1234,
    };
    uint8_t pkt[256];
    int n = fh_build_cplane_s3(&tx, EAXC, &s, pkt, sizeof(pkt));
    assert(n == (int)(ECPRI_HEADER_SIZE + 20u));

    fh_pkt_t p;
    assert(fh_parse_cplane(&rx, pkt, (size_t)n, &p) == ORU_OK);
    assert(p.kind == FH_PKT_CPLANE_S3);
    assert(p.cplane.s3.cp_length == 288);
    assert(p.cplane.s3.freq_offset == -1234);
}

static void test_uplane_roundtrip(void)
{
    fh_tx_t tx; fh_tx_init(&tx);
    fh_rx_t rx; fh_rx_init(&rx);

    enum { S0 = 2, S1 = 1 };
    oru_iq16_t iq0[S0 * 12], iq1[S1 * 12];
    for (int k = 0; k < S0 * 12; k++) { iq0[k].i = (int16_t)k; iq0[k].q = (int16_t)-k; }
    for (int k = 0; k < S1 * 12; k++) { iq1[k].i = (int16_t)(k+1); iq1[k].q = (int16_t)(k-1); }

    oran_radio_app_hdr_t app = {
        .data_direction = 0, .payload_version = 1, .frame_id = 3,
        .subframe_id = 1, .slot_id = 2, .start_symbol_id = 0,
    };
    oran_uplane_section_t secs[2] = {
        { .hdr = { .section_id = 1, .start_prb = 0, .num_prb = S0,
                   .comp_meth = ORAN_COMP_NONE, .iq_bitwidth = 16 },
          .iq = iq0, .n_samples = S0 * 12 },
        { .hdr = { .section_id = 2, .start_prb = 2, .num_prb = S1,
                   .comp_meth = ORAN_COMP_BFP, .iq_bitwidth = 9 },
          .iq = iq1, .n_samples = S1 * 12 },
    };

    uint8_t pkt[2048];
    int n = fh_build_uplane(&tx, EAXC, &app, secs, 2, pkt, sizeof(pkt));
    assert(n > 0);

    fh_pkt_t p;
    oran_uplane_msg_t msg;
    oru_iq16_t out[(S0 + S1) * 12];
    assert(fh_parse_uplane(&rx, pkt, (size_t)n, &p, &msg, out,
                           sizeof(out) / sizeof(out[0])) == ORU_OK);
    assert(p.kind == FH_PKT_UPLANE);
    assert(p.ecpri.msg_type == ECPRI_MSG_IQ_DATA);
    assert(msg.n_sections == 2);
    assert(msg.app.frame_id == 3);
    /* uncompressed section 0 exact */
    for (int k = 0; k < S0 * 12; k++) {
        assert(out[msg.sec_offsets[0] + k].i == iq0[k].i);
        assert(out[msg.sec_offsets[0] + k].q == iq0[k].q);
    }
}

/* Sequence ids must increment per eAxC, and a gap must be flagged. */
static void test_seq_tracking(void)
{
    fh_tx_t tx; fh_tx_init(&tx);
    fh_rx_t rx; fh_rx_init(&rx);

    oran_cplane_section_t s = { .num_prb = 10 };
    uint8_t a[256], b[256], c[256];
    int na = fh_build_cplane_s1(&tx, EAXC, &s, a, sizeof(a));
    int nb = fh_build_cplane_s1(&tx, EAXC, &s, b, sizeof(b));
    int nc = fh_build_cplane_s1(&tx, EAXC, &s, c, sizeof(c));
    assert(na > 0 && nb > 0 && nc > 0);

    fh_pkt_t p;
    /* in-order a, b -> both ok */
    assert(fh_parse_cplane(&rx, a, (size_t)na, &p) == ORU_OK && p.seq_ok);
    assert(fh_parse_cplane(&rx, b, (size_t)nb, &p) == ORU_OK && p.seq_ok);
    assert(rx.seq_gaps == 0);

    /* skip c's predecessor by feeding c (seq 2) then a (seq 0) again:
     * after b we expect seq 2; c is seq 2 -> ok; then a is seq 0 -> gap */
    assert(fh_parse_cplane(&rx, c, (size_t)nc, &p) == ORU_OK && p.seq_ok);
    assert(fh_parse_cplane(&rx, a, (size_t)na, &p) == ORU_OK && !p.seq_ok);
    assert(rx.seq_gaps == 1);
}

/* Distinct eAxC flows keep independent sequence counters. */
static void test_multi_eaxc(void)
{
    fh_tx_t tx; fh_tx_init(&tx);
    fh_rx_t rx; fh_rx_init(&rx);

    oran_cplane_section_t s = { .num_prb = 5 };
    uint8_t buf[256];

    int n = fh_build_cplane_s1(&tx, 0x10, &s, buf, sizeof(buf));
    fh_pkt_t p;
    assert(fh_parse_cplane(&rx, buf, (size_t)n, &p) == ORU_OK);
    assert(p.ecpri.pc_id == 0x10 && p.ecpri.seq_id == 0 && p.seq_ok);

    n = fh_build_cplane_s1(&tx, 0x20, &s, buf, sizeof(buf));
    assert(fh_parse_cplane(&rx, buf, (size_t)n, &p) == ORU_OK);
    assert(p.ecpri.pc_id == 0x20 && p.ecpri.seq_id == 0 && p.seq_ok);

    /* second packet on 0x10 should be seq 1 */
    n = fh_build_cplane_s1(&tx, 0x10, &s, buf, sizeof(buf));
    assert(fh_parse_cplane(&rx, buf, (size_t)n, &p) == ORU_OK);
    assert(p.ecpri.seq_id == 1 && p.seq_ok);
    assert(rx.seq_gaps == 0);
}

static void test_wrong_type(void)
{
    fh_tx_t tx; fh_tx_init(&tx);
    fh_rx_t rx; fh_rx_init(&rx);

    /* Build a U-plane packet but try to parse it as C-plane. */
    oru_iq16_t iq[12] = {0};
    oran_radio_app_hdr_t app = { .payload_version = 1 };
    oran_uplane_section_t sec = {
        .hdr = { .section_id = 1, .num_prb = 1,
                 .comp_meth = ORAN_COMP_NONE, .iq_bitwidth = 16 },
        .iq = iq, .n_samples = 12 };
    uint8_t pkt[512];
    int n = fh_build_uplane(&tx, EAXC, &app, &sec, 1, pkt, sizeof(pkt));
    assert(n > 0);

    fh_pkt_t p;
    assert(fh_parse_cplane(&rx, pkt, (size_t)n, &p) == ORU_ERR_PROTO);
}

int main(void)
{
    test_cplane_s1_roundtrip();
    test_cplane_s3_roundtrip();
    test_uplane_roundtrip();
    test_seq_tracking();
    test_multi_eaxc();
    test_wrong_type();
    printf("test_fh_packet: PASS\n");
    return 0;
}
