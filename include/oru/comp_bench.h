/* SPDX-License-Identifier: MIT */
/*
 * Compression benchmark harness for the U-plane codecs.
 *
 * Given a block of IQ samples, measures for a chosen compression method:
 *   - compressed size vs. the raw 16-bit size (compression ratio),
 *   - round-trip accuracy (max abs error and RMSE),
 * so the lossy methods (BFP / µ-law / modulation) can be compared on the
 * same signal. Pure measurement: no I/O, so it is unit-testable and can
 * also back a CLI report.
 */
#ifndef ORU_COMP_BENCH_H
#define ORU_COMP_BENCH_H

#include "oru/types.h"

typedef struct {
    uint8_t comp_meth;       /* oran_comp_meth_t                        */
    uint8_t iq_width;        /* per-component width / modulation order  */
    size_t  raw_bytes;       /* uncompressed 16-bit size                */
    size_t  comp_bytes;      /* compressed size                         */
    double  ratio;           /* raw_bytes / comp_bytes                  */
    int     max_abs_err;     /* worst single-component error            */
    double  rmse;            /* root-mean-square error across I and Q   */
} comp_bench_result_t;

/*
 * Run one method over `n_prb` PRBs (n_prb*12 samples). Compresses then
 * decompresses through the U-plane section codec and fills `out`.
 * Returns ORU_OK, or a negative status on encode/decode failure.
 */
oru_status_t comp_bench_run(const oru_iq16_t *iq, uint16_t n_prb,
                            uint8_t comp_meth, uint8_t iq_width,
                            comp_bench_result_t *out);

#endif /* ORU_COMP_BENCH_H */
