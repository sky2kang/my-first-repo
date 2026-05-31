/* SPDX-License-Identifier: MIT */
/*
 * Performance management (PM) counter aggregation.
 *
 * O-RAN's o-ran-performance-management collects measurement counters over
 * fixed measurement intervals (typically 15 min / 24 h) and reports them to
 * the SMO. This module is the host-side equivalent: a single place that
 * accumulates the counters the datapath/fronthaul subsystems produce, can
 * snapshot a measurement interval, and serialises the result to JSON
 * instance data shaped after the YANG performance-measurement tree.
 *
 * Subsystems push events through the perf_inc_* helpers (or fold in their
 * own stats structs), keeping the counter definitions in one place so PM
 * reporting does not have to reach into every module.
 */
#ifndef ORU_PERF_H
#define ORU_PERF_H

#include "oru/types.h"

/* The aggregated counter set (a measurement group). */
typedef struct {
    /* fronthaul U/C-plane traffic */
    uint64_t rx_packets;       /* fronthaul packets received OK          */
    uint64_t tx_packets;       /* fronthaul packets transmitted          */
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t seq_gaps;         /* eAxC sequence discontinuities          */

    /* timing-window outcomes */
    uint64_t dl_on_time;
    uint64_t dl_early;
    uint64_t dl_late;          /* dropped: missed the slot               */
    uint64_t ul_on_time;
    uint64_t ul_late;          /* emitted past Ta3 deadline               */

    /* C/U-plane pairing */
    uint64_t cu_matched;
    uint64_t cu_orphan;
    uint64_t cu_out_of_range;

    /* radio quality (running averages, scaled x100 for fixed point feel) */
    uint64_t evm_pct_x100_sum;  /* sum of EVM%*100 samples               */
    uint64_t evm_samples;       /* count, to derive the mean             */
} perf_counters_t;

typedef struct {
    perf_counters_t live;        /* counters accumulating now            */
    perf_counters_t last;        /* snapshot of the previous interval    */
    uint64_t        interval_id; /* increments each snapshot             */
} perf_t;

void perf_init(perf_t *p);

/* Fronthaul traffic events. */
void perf_rx_packet(perf_t *p, size_t bytes);
void perf_tx_packet(perf_t *p, size_t bytes);
void perf_seq_gap(perf_t *p);

/* Timing-window outcomes (mirrors fh_window_result_t per direction). */
void perf_dl_window(perf_t *p, int on_time, int early, int late);
void perf_ul_window(perf_t *p, int on_time, int late);

/* C/U-plane match outcomes. */
void perf_cu_result(perf_t *p, int matched, int orphan, int out_of_range);

/* Record an EVM% sample for the running average. */
void perf_evm_sample(perf_t *p, double evm_pct);

/* Mean EVM% over the live interval (0 if no samples). */
double perf_mean_evm_pct(const perf_t *p);

/*
 * Close the current measurement interval: copy live -> last, bump the
 * interval id, and zero the live counters for the next window.
 */
void perf_snapshot(perf_t *p);

/*
 * Serialise the last completed interval to JSON instance data shaped after
 * o-ran-performance-management. Writes a NUL-terminated string; returns the
 * byte count (excluding NUL) or a negative oru_status_t if buf is too small.
 */
int perf_to_json(const perf_t *p, char *buf, size_t buf_len);

#endif /* ORU_PERF_H */
