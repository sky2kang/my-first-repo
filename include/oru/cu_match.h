/* SPDX-License-Identifier: MIT */
/*
 * C-plane <-> U-plane time alignment matcher.
 *
 * In O-RAN 7.2x the DU first sends a C-plane section (Section Type 1/3) that
 * schedules a slot: "in frame F, subframe SF, slot SL, starting at symbol
 * SYM, expect IQ for PRBs [startPrb, startPrb+numPrb)". The matching U-plane
 * packets then carry the IQ for that grant. The O-RU must pair each U-plane
 * arrival with the C-plane grant that authorised it; IQ that does not match
 * any outstanding grant (wrong slot, PRBs outside the grant) is an error.
 *
 * This module keeps a small table of outstanding C-plane grants keyed by
 * (frame, subframe, slot, symbol) and checks U-plane sections against them:
 *   - matched + PRB range within the grant  -> ORU_OK (coverage tracked)
 *   - no grant for that slot/symbol          -> "orphan" U-plane
 *   - PRBs outside the grant                 -> "out of range"
 *
 * It lets us verify the scheduling/data pairing end to end on a host before
 * any real DU is connected.
 */
#ifndef ORU_CU_MATCH_H
#define ORU_CU_MATCH_H

#include "oru/types.h"
#include "oru/fronthaul.h"

#define CU_MATCH_MAX_GRANTS 32u

typedef enum {
    CU_MATCH_OK = 0,       /* U-plane section covered by a grant      */
    CU_MATCH_NO_GRANT,     /* no C-plane grant for this slot/symbol   */
    CU_MATCH_OUT_OF_RANGE, /* PRBs fall outside the matched grant     */
} cu_match_result_t;

/* One scheduled grant derived from a C-plane section. */
typedef struct {
    uint8_t  frame_id;
    uint8_t  subframe_id;
    uint8_t  slot_id;
    uint8_t  start_symbol_id;
    uint16_t start_prb;
    uint16_t num_prb;
    uint16_t covered_prb;  /* PRBs of this grant seen on U-plane so far */
    bool     in_use;
} cu_grant_t;

typedef struct {
    cu_grant_t grants[CU_MATCH_MAX_GRANTS];
    uint64_t   matched;       /* U-plane sections matched OK            */
    uint64_t   orphan;        /* U-plane with no grant                 */
    uint64_t   out_of_range;  /* U-plane PRBs outside grant            */
} cu_matcher_t;

void cu_match_init(cu_matcher_t *m);

/* Register a C-plane Section Type 1 grant. Returns ORU_ERR if the table is
 * full. A grant for an existing (frame,subframe,slot,symbol) is replaced. */
oru_status_t cu_match_add_section1(cu_matcher_t *m,
                                   const oran_cplane_section_t *s);

/* Same for a Section Type 3 grant (only the scheduling fields are used). */
oru_status_t cu_match_add_section3(cu_matcher_t *m,
                                   const oran_cplane_section3_t *s);

/* Check a U-plane section (by its header) against outstanding grants and
 * update counters and grant coverage. */
cu_match_result_t cu_match_check(cu_matcher_t *m,
                                 const oran_uplane_hdr_t *u);

/* A grant is "fully covered" once the U-plane PRBs seen reach numPrb. */
bool cu_match_grant_complete(const cu_matcher_t *m, uint8_t frame_id,
                             uint8_t subframe_id, uint8_t slot_id,
                             uint8_t start_symbol_id);

const char *cu_match_result_str(cu_match_result_t r);

#endif /* ORU_CU_MATCH_H */
