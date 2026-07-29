/* SPDX-License-Identifier: MIT */
/*
 * Digital beamforming weight application (downlink), modelled in the
 * frequency domain. See docs/03 (C-plane beamId).
 *
 * In O-RAN 7.2x the DU schedules a beam by beamId; the O-RU holds a
 * beamforming weight table that maps each beamId to a set of per-antenna
 * complex weights. For a single data stream s, the antenna outputs are:
 *
 *     y_a[n] = w_a * s[n]        for each antenna a in 0..num_ant-1
 *
 * where w_a is the (complex) weight for antenna a of the selected beam. The
 * weight phase steers the beam; the magnitude tapers it. This is the core
 * of analog/digital hybrid beamforming the O-RU performs before the IFFT
 * and RF chains.
 *
 * Weights are stored as Q1.15 fixed point (so +1.0 -> 32767), matching the
 * int16 IQ format used elsewhere. Applying a weight is a complex multiply
 * with rounding back to int16.
 */
#ifndef ORU_BEAMFORM_H
#define ORU_BEAMFORM_H

#include "oru/types.h"

#define BF_MAX_ANTENNAS 4u    /* ADRV9025 is 4T4R                       */
#define BF_MAX_BEAMS    16u   /* beam table capacity                    */
#define BF_UNITY        32767 /* Q1.15 representation of ~1.0           */

/* One complex weight in Q1.15. */
typedef struct {
    int16_t re;
    int16_t im;
} bf_weight_t;

/* Per-beam weight vector: one weight per antenna. */
typedef struct {
    uint16_t    beam_id;
    uint8_t     num_ant;
    bf_weight_t w[BF_MAX_ANTENNAS];
    bool        in_use;
} bf_beam_t;

typedef struct {
    bf_beam_t beams[BF_MAX_BEAMS];
    uint8_t   num_ant;     /* antennas this O-RU drives (1..4)          */
} bf_table_t;

/* Initialise an empty table for `num_ant` antennas (1..BF_MAX_ANTENNAS). */
oru_status_t bf_init(bf_table_t *t, uint8_t num_ant);

/* Add/replace the weight vector for a beamId. `w` has t->num_ant entries.
 * Returns ORU_ERR if the table is full. */
oru_status_t bf_set_beam(bf_table_t *t, uint16_t beam_id,
                         const bf_weight_t *w);

/*
 * Generate a uniform-linear-array steering beam at electrical angle
 * `theta_deg` (broadside = 0): w_a = exp(j * a * pi * sin(theta)) with unit
 * magnitude, for a half-wavelength-spaced ULA. Stores it under `beam_id`.
 */
oru_status_t bf_set_steering_beam(bf_table_t *t, uint16_t beam_id,
                                  double theta_deg);

/*
 * Apply beam `beam_id` to a single input stream `in` (n samples), producing
 * `num_ant` antenna streams. `out[a]` must hold n samples for antenna a.
 * Returns ORU_ERR_PARAM if the beam is unknown. */
oru_status_t bf_apply(const bf_table_t *t, uint16_t beam_id,
                      const oru_iq16_t *in, size_t n,
                      oru_iq16_t *out[BF_MAX_ANTENNAS]);

/* Look up a beam (NULL if absent). */
const bf_beam_t *bf_get_beam(const bf_table_t *t, uint16_t beam_id);

#endif /* ORU_BEAMFORM_H */
