/* SPDX-License-Identifier: MIT */
/* Tests for the fronthaul datapath with deadline enforcement. */
#include "oru/datapath.h"
#include "oru/hal.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const fh_sched_cfg_t CFG = {
    .t2a_min_ns = 100000, .t2a_max_ns = 300000,
    .ta3_min_ns = 50000,  .ta3_max_ns = 200000,
};

/* Build a single-section DL U-plane packet for `num_prb` PRBs. */
static int make_dl_packet(uint16_t num_prb, uint8_t *buf, size_t len)
{
    oru_iq16_t iq[64 * 12];
    size_t n = (size_t)num_prb * 12u;
    for (size_t k = 0; k < n; k++) {
        iq[k].i = (int16_t)(k & 0x3f);
        iq[k].q = (int16_t)-(int16_t)(k & 0x3f);
    }
    oran_uplane_hdr_t h = {
        .frame_id = 1, .num_prb = num_prb,
        .comp_meth = ORAN_COMP_NONE, .iq_bitwidth = 16,
    };
    return oran_uplane_encode(&h, iq, n, buf, len);
}

static void test_dl_on_time_delivered(void)
{
    datapath_t dp;
    assert(datapath_init(&dp, &CFG, 30000, 0, 0) == ORU_OK);

    uint8_t pkt[4096];
    int n = make_dl_packet(4, pkt, sizeof(pkt));
    assert(n > 0);

    uint64_t boundary = 10000000ull;
    uint64_t on_time  = boundary - 200000;  /* inside [b-300us, b-100us] */
    assert(datapath_handle_dl(&dp, pkt, (size_t)n, boundary, on_time) == ORU_OK);
    assert(dp.stats.dl_delivered == 1);
    assert(dp.stats.dl_dropped_late == 0);
}

static void test_dl_late_dropped(void)
{
    datapath_t dp;
    assert(datapath_init(&dp, &CFG, 30000, 0, 0) == ORU_OK);

    uint8_t pkt[4096];
    int n = make_dl_packet(4, pkt, sizeof(pkt));
    assert(n > 0);

    uint64_t boundary = 10000000ull;
    uint64_t late = boundary - 100000 + 1;  /* just past window close */
    assert(datapath_handle_dl(&dp, pkt, (size_t)n, boundary, late)
           == ORU_ERR_TIMEOUT);
    assert(dp.stats.dl_delivered == 0);
    assert(dp.stats.dl_dropped_late == 1);
}

static void test_ul_on_time_and_late(void)
{
    datapath_t dp;
    assert(datapath_init(&dp, &CFG, 30000, 0, 0) == ORU_OK);

    uint8_t pkt[4096];
    uint64_t boundary = 5000000ull;
    bool late = true;

    /* on time: within [b+50us, b+200us] */
    int n = datapath_build_ul(&dp, 2, ORAN_COMP_BFP, 9, boundary,
                              boundary + 100000, pkt, sizeof(pkt), &late);
    assert(n > 0);
    assert(!late);
    assert(dp.stats.ul_sent == 1 && dp.stats.ul_late == 0);

    /* late: past the Ta3 deadline -- still emitted, but flagged */
    n = datapath_build_ul(&dp, 2, ORAN_COMP_BFP, 9, boundary,
                          boundary + 200001, pkt, sizeof(pkt), &late);
    assert(n > 0);
    assert(late);
    assert(dp.stats.ul_sent == 2 && dp.stats.ul_late == 1);
}

int main(void)
{
    /* Bring the SIM HAL up so tx/rx IQ paths work, and lock JESD. */
    assert(hal_init(NULL) == ORU_OK);
    assert(hal_jesd204_bringup() == ORU_OK);
    assert(hal_jesd204_is_locked());

    test_dl_on_time_delivered();
    test_dl_late_dropped();
    test_ul_on_time_and_late();

    hal_shutdown();
    printf("test_datapath: PASS\n");
    return 0;
}
