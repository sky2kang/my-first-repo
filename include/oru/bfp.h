/* SPDX-License-Identifier: MIT */
/*
 * Block Floating Point (BFP) compression for O-RAN U-plane IQ.
 *
 * In O-RAN 7.2x the U-plane carries frequency-domain IQ one PRB at a time.
 * BFP (compMeth = 1) compresses each PRB independently:
 *
 *   - A PRB is 12 resource elements (REs); each RE is one complex sample
 *     (I and Q), so a PRB block is 24 signed values.
 *   - The block shares ONE exponent: the encoder finds the largest
 *     magnitude in the block, picks a right-shift (the exponent) so that
 *     every value fits into `iq_width` signed bits, then packs the shifted
 *     mantissas back to back.
 *
 * On-wire layout produced here, per PRB:
 *
 *     +----------+---------------------------------------------+
 *     | exponent |  24 mantissas, each `iq_width` bits, MSB-1st |
 *     | (1 byte) |  (order: I0,Q0, I1,Q1, ... I11,Q11)          |
 *     +----------+---------------------------------------------+
 *
 * This is lossy whenever the chosen exponent is > 0 (low bits are dropped),
 * which is the normal BFP trade-off. When the whole block already fits in
 * `iq_width` bits the exponent is 0 and the round-trip is exact.
 *
 * `iq_width` is the per-component bit width (e.g. 9 bits is common). Valid
 * range here is 2..16.
 */
#ifndef ORU_BFP_H
#define ORU_BFP_H

#include "oru/types.h"

#define BFP_RE_PER_PRB    12u
#define BFP_VALS_PER_PRB  (BFP_RE_PER_PRB * 2u)  /* I + Q */

/* Bytes of compressed output for one PRB at the given width. */
size_t bfp_prb_bytes(uint8_t iq_width);

/*
 * Compress `n_prb` PRBs (so n_prb * 12 complex samples in `iq`) into `out`.
 * Returns the number of bytes written, or a negative oru_status_t.
 */
int bfp_compress(const oru_iq16_t *iq, size_t n_prb, uint8_t iq_width,
                 uint8_t *out, size_t out_len);

/*
 * Decompress `n_prb` PRBs from `in` into `iq` (must hold n_prb*12 samples).
 * Returns the number of complex samples written, or a negative
 * oru_status_t.
 */
int bfp_decompress(const uint8_t *in, size_t in_len, size_t n_prb,
                   uint8_t iq_width, oru_iq16_t *iq, size_t max_samples);

#endif /* ORU_BFP_H */
