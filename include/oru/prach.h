/* SPDX-License-Identifier: MIT */
/*
 * PRACH (Physical Random Access Channel) preamble generation and detection,
 * modelled in the frequency domain. See docs/03 (Section Type 3) for how
 * the DU schedules a PRACH occasion; this module is the signal-processing
 * core the O-RU runs inside that occasion.
 *
 * 5G NR PRACH preambles are Zadoff-Chu (ZC) sequences: constant-amplitude
 * sequences with the useful property that the cyclic cross-correlation of a
 * ZC sequence with a cyclically shifted copy of itself is an impulse. A
 * preamble is defined by a logical root index (-> physical root u) and a
 * cyclic shift. Detection correlates the received sequence against the
 * known root at every candidate shift and picks the strongest peak.
 *
 * We use a short ZC length (Nzc = 139, an NR short-preamble length) so the
 * whole thing runs cheaply on a host. IQ are the fixed-point oru_iq16_t we
 * use elsewhere; internally we work in double for the correlation.
 */
#ifndef ORU_PRACH_H
#define ORU_PRACH_H

#include "oru/types.h"

#define PRACH_NZC 139u   /* Zadoff-Chu sequence length (NR short format) */

/* Generate the ZC preamble for logical root `root` (0..PRACH_NZC-1) and a
 * cyclic shift `shift` (in samples) into `out` (PRACH_NZC complex samples).
 * Amplitude scales the unit-circle points to int16. Returns ORU_OK. */
oru_status_t prach_gen_preamble(uint16_t root, uint16_t shift,
                                int16_t amplitude, oru_iq16_t *out);

/* Result of a detection attempt. */
typedef struct {
    bool     detected;     /* peak/avg ratio exceeded the threshold     */
    uint16_t shift;        /* estimated cyclic shift of the peak        */
    double   peak;         /* peak correlation magnitude                */
    double   mean;         /* mean correlation magnitude across shifts  */
    double   ratio;        /* peak / mean (detection metric)            */
} prach_detect_t;

/*
 * Correlate `rx` (PRACH_NZC samples) against the ZC sequence for `root`
 * over all cyclic shifts and report the strongest peak. `threshold` is the
 * minimum peak/mean ratio to declare a detection. Returns ORU_OK (the
 * decision is in out->detected) or a negative status on bad input.
 */
oru_status_t prach_detect(const oru_iq16_t *rx, uint16_t root,
                          double threshold, prach_detect_t *out);

#endif /* ORU_PRACH_H */
