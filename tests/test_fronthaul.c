/* SPDX-License-Identifier: MIT */
/* Round-trip tests for O-RAN C-plane and U-plane (sub)encoders. */
#include "oru/fronthaul.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_cplane(void)
{
    oran_cplane_section_t in = {
        .frame_id = 7, .subframe_id = 3, .slot_id = 1,
        .start_symbol_id = 2, .start_prb = 0, .num_prb = 273,
        .beam_id = 42,
    };
    uint8_t buf[32];
    int n = oran_cplane_encode(&in, buf, sizeof(buf));
    assert(n > 0);

    oran_cplane_section_t out;
    assert(oran_cplane_decode(buf, (size_t)n, &out) == ORU_OK);
    assert(out.frame_id == in.frame_id);
    assert(out.num_prb == in.num_prb);
    assert(out.beam_id == in.beam_id);
}

static void test_cplane3(void)
{
    oran_cplane_section3_t in = {
        .frame_id = 9, .subframe_id = 1, .slot_id = 0,
        .start_symbol_id = 0, .start_prb = 6, .num_prb = 12,
        .beam_id = 1, .time_offset = 4096, .frame_structure = 0x31,
        .cp_length = 288, .freq_offset = -1234,
    };
    uint8_t buf[32];
    int n = oran_cplane3_encode(&in, buf, sizeof(buf));
    assert(n > 0);

    oran_cplane_section3_t out;
    assert(oran_cplane3_decode(buf, (size_t)n, &out) == ORU_OK);
    assert(out.frame_id == in.frame_id);
    assert(out.num_prb == in.num_prb);
    assert(out.time_offset == in.time_offset);
    assert(out.frame_structure == in.frame_structure);
    assert(out.cp_length == in.cp_length);
    assert(out.freq_offset == in.freq_offset);   /* incl. negative round-trip */

    /* positive freq offset too */
    in.freq_offset = 5000;
    n = oran_cplane3_encode(&in, buf, sizeof(buf));
    assert(n > 0 && oran_cplane3_decode(buf, (size_t)n, &out) == ORU_OK);
    assert(out.freq_offset == 5000);
}

/* Round-trip one U-plane symbol with a given compression method. With
 * ORAN_COMP_NONE the result is exact; with BFP it is exact only when the
 * samples already fit in iq_width bits (exponent stays 0). */
static void uplane_roundtrip(uint8_t comp_meth, uint8_t width, int amplitude,
                             int expect_exact)
{
    enum { NPRB = 4, NRE = NPRB * 12 };
    oru_iq16_t iq[NRE];
    for (int k = 0; k < NRE; k++) {
        iq[k].i = (int16_t)((k % amplitude) - amplitude / 2);
        iq[k].q = (int16_t)(amplitude / 2 - (k % amplitude));
    }

    oran_uplane_hdr_t h = {
        .frame_id = 1, .subframe_id = 2, .slot_id = 3, .symbol_id = 4,
        .start_prb = 0, .num_prb = NPRB,
        .comp_meth = comp_meth, .iq_bitwidth = width,
    };

    uint8_t buf[2048];
    int n = oran_uplane_encode(&h, iq, NRE, buf, sizeof(buf));
    assert(n > 0);

    oran_uplane_hdr_t oh;
    oru_iq16_t out[NRE];
    size_t got = 0;
    assert(oran_uplane_decode(buf, (size_t)n, &oh, out, NRE, &got) == ORU_OK);
    assert(got == (size_t)NRE);
    assert(oh.num_prb == NPRB);
    assert(oh.comp_meth == comp_meth);

    if (expect_exact) {
        for (int k = 0; k < NRE; k++) {
            assert(out[k].i == iq[k].i);
            assert(out[k].q == iq[k].q);
        }
    } else {
        /* lossy: decoded value must be within one quantization step
         * (2^exponent) of the original. We don't know the exponent here,
         * so just assert the error is bounded and the sign/scale survive. */
        for (int k = 0; k < NRE; k++) {
            int di = (int)out[k].i - (int)iq[k].i;
            int dq = (int)out[k].q - (int)iq[k].q;
            assert(di > -256 && di < 256);
            assert(dq > -256 && dq < 256);
        }
    }
}

static void test_uplane(void)
{
    /* uncompressed: always exact */
    uplane_roundtrip(ORAN_COMP_NONE, 16, 200, 1);
    /* BFP, small amplitude that fits in 9 bits -> exponent 0 -> exact */
    uplane_roundtrip(ORAN_COMP_BFP, 9, 200, 1);
    /* BFP, large amplitude -> nonzero exponent -> bounded loss */
    uplane_roundtrip(ORAN_COMP_BFP, 9, 30000, 0);
    /* BFP at other widths */
    uplane_roundtrip(ORAN_COMP_BFP, 12, 1000, 1);
    uplane_roundtrip(ORAN_COMP_BFP, 8, 100, 1);
    /* µ-law: lossy companding, error bounded (< 256 for small amplitudes) */
    uplane_roundtrip(ORAN_COMP_MULAW, 8, 100, 0);
    uplane_roundtrip(ORAN_COMP_MULAW, 12, 5000, 0);
}

/* Multi-section U-plane message: 3 sections with mixed compression. */
static void test_uplane_msg(void)
{
    enum { S0 = 2, S1 = 1, S2 = 3 };  /* PRB counts per section */
    oru_iq16_t iq0[S0 * 12], iq1[S1 * 12], iq2[S2 * 12];
    for (int k = 0; k < S0 * 12; k++) { iq0[k].i = (int16_t)(k);     iq0[k].q = (int16_t)(-k); }
    for (int k = 0; k < S1 * 12; k++) { iq1[k].i = (int16_t)(k + 5); iq1[k].q = (int16_t)(k - 5); }
    for (int k = 0; k < S2 * 12; k++) { iq2[k].i = (int16_t)(50 - k); iq2[k].q = (int16_t)(k); }

    oran_radio_app_hdr_t app = {
        .data_direction = 1, .payload_version = 1, .filter_index = 0,
        .frame_id = 12, .subframe_id = 4, .slot_id = 1, .start_symbol_id = 0,
    };
    oran_uplane_section_t secs[3] = {
        { .hdr = { .section_id = 100, .start_prb = 0,  .num_prb = S0,
                   .comp_meth = ORAN_COMP_NONE, .iq_bitwidth = 16 },
          .iq = iq0, .n_samples = S0 * 12 },
        { .hdr = { .section_id = 101, .start_prb = 2,  .num_prb = S1,
                   .comp_meth = ORAN_COMP_BFP,  .iq_bitwidth = 9 },
          .iq = iq1, .n_samples = S1 * 12 },
        { .hdr = { .section_id = 102, .start_prb = 3,  .num_prb = S2,
                   .comp_meth = ORAN_COMP_NONE, .iq_bitwidth = 16 },
          .iq = iq2, .n_samples = S2 * 12 },
    };

    uint8_t buf[2048];
    int n = oran_uplane_msg_encode(&app, secs, 3, buf, sizeof(buf));
    assert(n > 0);

    oran_uplane_msg_t msg;
    oru_iq16_t out[(S0 + S1 + S2) * 12];
    assert(oran_uplane_msg_decode(buf, (size_t)n, &msg, out,
                                  sizeof(out) / sizeof(out[0])) == ORU_OK);

    assert(msg.n_sections == 3);
    assert(msg.app.frame_id == 12 && msg.app.data_direction == 1);
    assert(msg.sec_hdrs[0].section_id == 100);
    assert(msg.sec_hdrs[1].section_id == 101);
    assert(msg.sec_hdrs[2].section_id == 102);
    assert(msg.n_samples == (S0 + S1 + S2) * 12);

    /* Section 0 (uncompressed) must round-trip exactly. */
    for (int k = 0; k < S0 * 12; k++) {
        assert(out[msg.sec_offsets[0] + k].i == iq0[k].i);
        assert(out[msg.sec_offsets[0] + k].q == iq0[k].q);
    }
    /* Section 2 (uncompressed) too. */
    for (int k = 0; k < S2 * 12; k++) {
        assert(out[msg.sec_offsets[2] + k].i == iq2[k].i);
        assert(out[msg.sec_offsets[2] + k].q == iq2[k].q);
    }
    /* Section 1 used small-amplitude BFP@9 -> exponent 0 -> exact. */
    for (int k = 0; k < S1 * 12; k++) {
        assert(out[msg.sec_offsets[1] + k].i == iq1[k].i);
        assert(out[msg.sec_offsets[1] + k].q == iq1[k].q);
    }

    /* Guard: zero sections rejected. */
    assert(oran_uplane_msg_encode(&app, secs, 0, buf, sizeof(buf)) < 0);
}

int main(void)
{
    test_cplane();
    test_cplane3();
    test_uplane();
    test_uplane_msg();
    printf("test_fronthaul: PASS\n");
    return 0;
}
