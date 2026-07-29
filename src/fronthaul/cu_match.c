/* SPDX-License-Identifier: MIT */
#include "oru/cu_match.h"
#include "oru/log.h"

#include <string.h>

#define TAG "cu_match"

const char *cu_match_result_str(cu_match_result_t r)
{
    switch (r) {
    case CU_MATCH_OK:           return "OK";
    case CU_MATCH_NO_GRANT:     return "NO_GRANT";
    case CU_MATCH_OUT_OF_RANGE: return "OUT_OF_RANGE";
    default:                    return "?";
    }
}

void cu_match_init(cu_matcher_t *m)
{
    if (m)
        memset(m, 0, sizeof(*m));
}

/* Do two grants/sections address the same slot+symbol? */
static bool same_slot(const cu_grant_t *g, uint8_t f, uint8_t sf,
                      uint8_t sl, uint8_t sym)
{
    return g->in_use && g->frame_id == f && g->subframe_id == sf &&
           g->slot_id == sl && g->start_symbol_id == sym;
}

/* Find a grant for the slot/symbol, or NULL. */
static cu_grant_t *find_grant(cu_matcher_t *m, uint8_t f, uint8_t sf,
                              uint8_t sl, uint8_t sym)
{
    for (size_t i = 0; i < CU_MATCH_MAX_GRANTS; i++)
        if (same_slot(&m->grants[i], f, sf, sl, sym))
            return &m->grants[i];
    return NULL;
}

/* Insert/replace a grant. Returns the slot pointer or NULL if table full. */
static cu_grant_t *alloc_grant(cu_matcher_t *m, uint8_t f, uint8_t sf,
                               uint8_t sl, uint8_t sym)
{
    cu_grant_t *g = find_grant(m, f, sf, sl, sym);
    if (g)
        return g;                 /* replace existing */
    for (size_t i = 0; i < CU_MATCH_MAX_GRANTS; i++)
        if (!m->grants[i].in_use)
            return &m->grants[i];
    return NULL;
}

oru_status_t cu_match_add_section1(cu_matcher_t *m,
                                   const oran_cplane_section_t *s)
{
    if (!m || !s)
        return ORU_ERR_PARAM;

    cu_grant_t *g = alloc_grant(m, s->frame_id, s->subframe_id,
                                s->slot_id, s->start_symbol_id);
    if (!g) {
        LOGW(TAG, "grant table full, dropping section1");
        return ORU_ERR;
    }
    g->frame_id        = s->frame_id;
    g->subframe_id     = s->subframe_id;
    g->slot_id         = s->slot_id;
    g->start_symbol_id = s->start_symbol_id;
    g->start_prb       = s->start_prb;
    g->num_prb         = s->num_prb;
    g->covered_prb     = 0;
    g->in_use          = true;
    return ORU_OK;
}

oru_status_t cu_match_add_section3(cu_matcher_t *m,
                                   const oran_cplane_section3_t *s)
{
    if (!m || !s)
        return ORU_ERR_PARAM;

    cu_grant_t *g = alloc_grant(m, s->frame_id, s->subframe_id,
                                s->slot_id, s->start_symbol_id);
    if (!g) {
        LOGW(TAG, "grant table full, dropping section3");
        return ORU_ERR;
    }
    g->frame_id        = s->frame_id;
    g->subframe_id     = s->subframe_id;
    g->slot_id         = s->slot_id;
    g->start_symbol_id = s->start_symbol_id;
    g->start_prb       = s->start_prb;
    g->num_prb         = s->num_prb;
    g->covered_prb     = 0;
    g->in_use          = true;
    return ORU_OK;
}

cu_match_result_t cu_match_check(cu_matcher_t *m, const oran_uplane_hdr_t *u)
{
    if (!m || !u)
        return CU_MATCH_NO_GRANT;

    cu_grant_t *g = find_grant(m, u->frame_id, u->subframe_id,
                               u->slot_id, u->symbol_id);
    if (!g) {
        m->orphan++;
        LOGD(TAG, "orphan U-plane f=%u sf=%u sl=%u sym=%u (no grant)",
             u->frame_id, u->subframe_id, u->slot_id, u->symbol_id);
        return CU_MATCH_NO_GRANT;
    }

    /* The U-plane PRB range must fall within the grant's PRB range. */
    uint32_t u_end = (uint32_t)u->start_prb + u->num_prb;
    uint32_t g_end = (uint32_t)g->start_prb + g->num_prb;
    if (u->start_prb < g->start_prb || u_end > g_end) {
        m->out_of_range++;
        LOGD(TAG, "U-plane PRBs [%u..%u) outside grant [%u..%u)",
             u->start_prb, u_end, g->start_prb, g_end);
        return CU_MATCH_OUT_OF_RANGE;
    }

    g->covered_prb = (uint16_t)(g->covered_prb + u->num_prb);
    m->matched++;
    return CU_MATCH_OK;
}

bool cu_match_grant_complete(const cu_matcher_t *m, uint8_t frame_id,
                             uint8_t subframe_id, uint8_t slot_id,
                             uint8_t start_symbol_id)
{
    if (!m)
        return false;
    for (size_t i = 0; i < CU_MATCH_MAX_GRANTS; i++) {
        const cu_grant_t *g = &m->grants[i];
        if (same_slot(g, frame_id, subframe_id, slot_id, start_symbol_id))
            return g->covered_prb >= g->num_prb;
    }
    return false;
}
