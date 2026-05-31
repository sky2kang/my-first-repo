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

static void test_uplane(void)
{
    enum { NPRB = 4, NRE = NPRB * 12 };
    oru_iq16_t iq[NRE];
    for (int k = 0; k < NRE; k++) {
        iq[k].i = (int16_t)(k - 10);
        iq[k].q = (int16_t)(100 - k);
    }

    oran_uplane_hdr_t h = {
        .frame_id = 1, .subframe_id = 2, .slot_id = 3, .symbol_id = 4,
        .start_prb = 0, .num_prb = NPRB, .iq_bitwidth = 16,
    };

    uint8_t buf[512];
    int n = oran_uplane_encode(&h, iq, NRE, buf, sizeof(buf));
    assert(n > 0);

    oran_uplane_hdr_t oh;
    oru_iq16_t out[NRE];
    size_t got = 0;
    assert(oran_uplane_decode(buf, (size_t)n, &oh, out, NRE, &got) == ORU_OK);
    assert(got == (size_t)NRE);
    assert(oh.num_prb == NPRB);
    for (int k = 0; k < NRE; k++) {
        assert(out[k].i == iq[k].i);
        assert(out[k].q == iq[k].q);
    }
}

int main(void)
{
    test_cplane();
    test_uplane();
    printf("test_fronthaul: PASS\n");
    return 0;
}
