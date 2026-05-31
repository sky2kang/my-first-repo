/* SPDX-License-Identifier: MIT */
/*
 * µ-law companding for O-RAN U-plane IQ (udCompHdr compMeth = 3).
 *
 * Unlike BFP (which shares one exponent per PRB and is otherwise linear),
 * µ-law is a non-linear companding: it allocates more code points to small
 * magnitudes and fewer to large ones, matching the logarithmic sensitivity
 * of many signals. It is fixed-rate -- every component is encoded to the
 * same `iq_width` bits regardless of the block contents -- so the on-wire
 * size depends only on width, not on the data.
 *
 * Encoding (per signed 16-bit component x, output width w, µ = 255):
 *
 *     y = sgn(x) * round( (2^(w-1)-1) * ln(1 + µ|x|/X) / ln(1+µ) )
 *
 * where X = 32768 is the full-scale magnitude. Decoding inverts it:
 *
 *     |x| = (X/µ) * ( (1+µ)^(|y|/(2^(w-1)-1)) - 1 )
 *
 * This is lossy (companding quantisation), but preserves small-signal
 * fidelity better than uniform quantisation at the same width.
 *
 * On-wire layout: I and Q packed back to back, each `iq_width` bits,
 * MSB-first, in the order I0,Q0, I1,Q1, ... (no per-block header).
 */
#ifndef ORU_MULAW_H
#define ORU_MULAW_H

#include "oru/types.h"

/* Bytes of compressed output for `n_samples` complex samples at `iq_width`
 * bits per component. */
size_t mulaw_bytes(size_t n_samples, uint8_t iq_width);

/*
 * Compand `n_samples` complex samples into `out`. Returns bytes written,
 * or a negative oru_status_t. Valid iq_width is 4..16.
 */
int mulaw_compress(const oru_iq16_t *iq, size_t n_samples, uint8_t iq_width,
                   uint8_t *out, size_t out_len);

/*
 * Expand `n_samples` complex samples from `in` into `iq`. Returns the
 * number of complex samples written, or a negative oru_status_t.
 */
int mulaw_decompress(const uint8_t *in, size_t in_len, size_t n_samples,
                     uint8_t iq_width, oru_iq16_t *iq, size_t max_samples);

#endif /* ORU_MULAW_H */
