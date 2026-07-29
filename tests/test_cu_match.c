/* SPDX-License-Identifier: MIT */
/* Tests for the C-plane <-> U-plane time-alignment matcher. */
#include "oru/cu_match.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static oran_cplane_section_t grant(uint8_t f, uint8_t sl, uint8_t sym,
                                   uint16_t sprb, uint16_t nprb)
{
    oran_cplane_section_t s;
    memset(&s, 0, sizeof(s));
    s.frame_id = f; s.slot_id = sl; s.start_symbol_id = sym;
    s.start_prb = sprb; s.num_prb = nprb;
    return s;
}

static oran_uplane_hdr_t up(uint8_t f, uint8_t sl, uint8_t sym,
                            uint16_t sprb, uint16_t nprb)
{
    oran_uplane_hdr_t u;
    memset(&u, 0, sizeof(u));
    u.frame_id = f; u.slot_id = sl; u.symbol_id = sym;
    u.start_prb = sprb; u.num_prb = nprb;
    return u;
}

static void test_match_ok_and_complete(void)
{
    cu_matcher_t m;
    cu_match_init(&m);

    oran_cplane_section_t g = grant(1, 2, 0, 0, 100);
    assert(cu_match_add_section1(&m, &g) == ORU_OK);

    /* two U-plane sections covering [0,50) and [50,100) complete the grant */
    oran_uplane_hdr_t u1 = up(1, 2, 0, 0, 50);
    oran_uplane_hdr_t u2 = up(1, 2, 0, 50, 50);
    /* grant_complete args: (frame, subframe, slot, symbol) = (1, 0, 2, 0) */
    assert(cu_match_check(&m, &u1) == CU_MATCH_OK);
    assert(!cu_match_grant_complete(&m, 1, 0, 2, 0));   /* only half */
    assert(cu_match_check(&m, &u2) == CU_MATCH_OK);
    assert(cu_match_grant_complete(&m, 1, 0, 2, 0));    /* now full */

    assert(m.matched == 2 && m.orphan == 0 && m.out_of_range == 0);
}

static void test_orphan(void)
{
    cu_matcher_t m;
    cu_match_init(&m);

    /* U-plane for a slot with no grant */
    oran_uplane_hdr_t u = up(3, 1, 0, 0, 10);
    assert(cu_match_check(&m, &u) == CU_MATCH_NO_GRANT);
    assert(m.orphan == 1 && m.matched == 0);
}

static void test_out_of_range(void)
{
    cu_matcher_t m;
    cu_match_init(&m);

    oran_cplane_section_t g = grant(1, 0, 0, 10, 20);   /* [10,30) */
    assert(cu_match_add_section1(&m, &g) == ORU_OK);

    oran_uplane_hdr_t below = up(1, 0, 0, 5, 10);        /* starts before */
    oran_uplane_hdr_t above = up(1, 0, 0, 25, 10);       /* ends after */
    assert(cu_match_check(&m, &below) == CU_MATCH_OUT_OF_RANGE);
    assert(cu_match_check(&m, &above) == CU_MATCH_OUT_OF_RANGE);
    assert(m.out_of_range == 2 && m.matched == 0);
}

static void test_section3_grant(void)
{
    cu_matcher_t m;
    cu_match_init(&m);

    oran_cplane_section3_t s3;
    memset(&s3, 0, sizeof(s3));
    s3.frame_id = 9; s3.slot_id = 0; s3.start_symbol_id = 0;
    s3.start_prb = 6; s3.num_prb = 12;
    assert(cu_match_add_section3(&m, &s3) == ORU_OK);

    oran_uplane_hdr_t u = up(9, 0, 0, 6, 12);
    assert(cu_match_check(&m, &u) == CU_MATCH_OK);
    assert(cu_match_grant_complete(&m, 9, 0, 0, 0));
}

static void test_table_full(void)
{
    cu_matcher_t m;
    cu_match_init(&m);
    /* fill all slots with distinct symbols */
    for (unsigned i = 0; i < CU_MATCH_MAX_GRANTS; i++) {
        oran_cplane_section_t g = grant(0, 0, (uint8_t)i, 0, 1);
        assert(cu_match_add_section1(&m, &g) == ORU_OK);
    }
    oran_cplane_section_t extra = grant(7, 7, 99, 0, 1);
    assert(cu_match_add_section1(&m, &extra) == ORU_ERR);
}

int main(void)
{
    test_match_ok_and_complete();
    test_orphan();
    test_out_of_range();
    test_section3_grant();
    test_table_full();
    printf("test_cu_match: PASS\n");
    return 0;
}
