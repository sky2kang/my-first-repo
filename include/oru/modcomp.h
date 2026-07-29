/* SPDX-License-Identifier: MIT */
/*
 * Modulation compression for O-RAN U-plane IQ (udCompHdr compMeth = 4).
 *
 * Modulation compression exploits that, for a known modulation order, the
 * transmitted constellation points lie on a finite grid. Instead of sending
 * full IQ, the DU sends a scaler plus the per-RE constellation indices. The
 * O-RU reconstructs IQ = scaler * constellation[index].
 *
 * This teaching-grade model encodes, per PRB block:
 *
 *     +----------+-------------------------------------------------+
 *     | csf+mod  | scaler  | nbits-per-RE constellation indices ...  |
 *     | (1 byte) | (2 B)   | (12 REs, MSB-first packed)              |
 *     +----------+-------------------------------------------------+
 *
 *   - byte 0: [7] constellation-shift flag (csf, unused here, 0)
 *             [3:0] modulation order log2 (1=BPSK, 2=QPSK, 4=16QAM, 6=64QAM)
 *   - bytes 1-2: scaler (Q1.15 unsigned magnitude shared by the block)
 *   - then one index per RE, each `mod_bits` wide.
 *
 * It is lossy: the encoder maps each RE to the nearest grid point for the
 * configured order, so it is exact only when the input already sits on that
 * constellation (as it does for a clean modulator). `iq_width` is reused as
 * the modulation order (bits/symbol) here, matching how O-RAN overloads the
 * udIqWidth field for compMeth 4.
 */
#ifndef ORU_MODCOMP_H
#define ORU_MODCOMP_H

#include "oru/types.h"

#define MODCOMP_RE_PER_PRB 12u

/* Bytes of compressed output for one PRB at the given modulation order
 * (bits/symbol). 0 if mod_bits is unsupported. */
size_t modcomp_prb_bytes(uint8_t mod_bits);

/* Compress n_prb PRBs (n_prb*12 samples) using the given modulation order.
 * Returns bytes written or a negative oru_status_t. */
int modcomp_compress(const oru_iq16_t *iq, size_t n_prb, uint8_t mod_bits,
                     uint8_t *out, size_t out_len);

/* Decompress n_prb PRBs. Returns complex samples written or negative. */
int modcomp_decompress(const uint8_t *in, size_t in_len, size_t n_prb,
                       uint8_t mod_bits, oru_iq16_t *iq, size_t max_samples);

#endif /* ORU_MODCOMP_H */
