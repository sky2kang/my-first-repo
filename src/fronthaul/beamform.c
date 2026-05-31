/* SPDX-License-Identifier: MIT */
#include "oru/beamform.h"
#include "oru/log.h"

#include <math.h>
#include <string.h>

#define TAG "beamform"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

oru_status_t bf_init(bf_table_t *t, uint8_t num_ant)
{
    if (!t || num_ant < 1 || num_ant > BF_MAX_ANTENNAS)
        return ORU_ERR_PARAM;
    memset(t, 0, sizeof(*t));
    t->num_ant = num_ant;
    LOGI(TAG, "init: %u antenna(s)", num_ant);
    return ORU_OK;
}

static bf_beam_t *find_or_alloc(bf_table_t *t, uint16_t beam_id)
{
    bf_beam_t *free_slot = NULL;
    for (size_t i = 0; i < BF_MAX_BEAMS; i++) {
        if (t->beams[i].in_use && t->beams[i].beam_id == beam_id)
            return &t->beams[i];
        if (!t->beams[i].in_use && !free_slot)
            free_slot = &t->beams[i];
    }
    return free_slot;
}

oru_status_t bf_set_beam(bf_table_t *t, uint16_t beam_id,
                         const bf_weight_t *w)
{
    if (!t || !w)
        return ORU_ERR_PARAM;

    bf_beam_t *b = find_or_alloc(t, beam_id);
    if (!b) {
        LOGW(TAG, "beam table full, dropping beam %u", beam_id);
        return ORU_ERR;
    }
    b->beam_id = beam_id;
    b->num_ant = t->num_ant;
    memcpy(b->w, w, sizeof(bf_weight_t) * t->num_ant);
    b->in_use = true;
    return ORU_OK;
}

oru_status_t bf_set_steering_beam(bf_table_t *t, uint16_t beam_id,
                                  double theta_deg)
{
    if (!t)
        return ORU_ERR_PARAM;

    /* Half-wavelength ULA: phase increment per antenna = pi * sin(theta). */
    double phi = M_PI * sin(theta_deg * M_PI / 180.0);
    bf_weight_t w[BF_MAX_ANTENNAS];
    for (uint8_t a = 0; a < t->num_ant; a++) {
        double ang = (double)a * phi;
        w[a].re = (int16_t)lround(cos(ang) * (double)BF_UNITY);
        w[a].im = (int16_t)lround(sin(ang) * (double)BF_UNITY);
    }
    return bf_set_beam(t, beam_id, w);
}

const bf_beam_t *bf_get_beam(const bf_table_t *t, uint16_t beam_id)
{
    if (!t)
        return NULL;
    for (size_t i = 0; i < BF_MAX_BEAMS; i++)
        if (t->beams[i].in_use && t->beams[i].beam_id == beam_id)
            return &t->beams[i];
    return NULL;
}

/* Complex multiply (a * b) in Q1.15, rounded, saturated to int16. */
static oru_iq16_t cmul_q15(oru_iq16_t a, bf_weight_t b)
{
    /* (ar + j ai)(br + j bi) = (ar*br - ai*bi) + j(ar*bi + ai*br), >>15 */
    int32_t re = ((int32_t)a.i * b.re - (int32_t)a.q * b.im);
    int32_t im = ((int32_t)a.i * b.im + (int32_t)a.q * b.re);
    re = (re + (1 << 14)) >> 15;
    im = (im + (1 << 14)) >> 15;
    if (re > 32767) re = 32767; else if (re < -32768) re = -32768;
    if (im > 32767) im = 32767; else if (im < -32768) im = -32768;
    oru_iq16_t r = { (int16_t)re, (int16_t)im };
    return r;
}

oru_status_t bf_apply(const bf_table_t *t, uint16_t beam_id,
                      const oru_iq16_t *in, size_t n,
                      oru_iq16_t *out[BF_MAX_ANTENNAS])
{
    if (!t || !in || !out)
        return ORU_ERR_PARAM;

    const bf_beam_t *b = bf_get_beam(t, beam_id);
    if (!b) {
        LOGD(TAG, "unknown beam %u", beam_id);
        return ORU_ERR_PARAM;
    }

    for (uint8_t a = 0; a < b->num_ant; a++) {
        if (!out[a])
            return ORU_ERR_PARAM;
        for (size_t k = 0; k < n; k++)
            out[a][k] = cmul_q15(in[k], b->w[a]);
    }
    return ORU_OK;
}
