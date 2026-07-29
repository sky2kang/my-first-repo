/* SPDX-License-Identifier: MIT */
/*
 * Slot-cadence processing loop for the fronthaul datapath.
 *
 * A real O-RU runs RX/TX worker threads that wake on each slot boundary
 * (derived from the PTP timescale) and service that slot's U-plane work
 * within the T2a/Ta3 windows. To keep this testable on a host without real
 * threads or PTP, the loop is driven by:
 *
 *   - a pluggable monotonic clock (now_ns), and
 *   - a per-slot work callback the caller supplies.
 *
 * On the target, now_ns would read the PTP-disciplined clock and the loop
 * would run on a dedicated, CPU-pinned thread; the per-slot logic is
 * identical either way.
 */
#ifndef ORU_SLOT_LOOP_H
#define ORU_SLOT_LOOP_H

#include "oru/types.h"
#include "oru/datapath.h"

/* Returns the current time in nanoseconds on the (PTP) timescale. */
typedef uint64_t (*slot_clock_fn)(void *ctx);

/* Called once per slot boundary. `slot` counts from the loop's start slot;
 * `boundary_ns` is that slot's boundary time. Return non-OK to stop early. */
typedef oru_status_t (*slot_work_fn)(void *ctx, uint64_t slot,
                                     uint64_t boundary_ns, datapath_t *dp);

typedef struct {
    datapath_t   *dp;
    slot_clock_fn clock;
    slot_work_fn  work;
    void         *ctx;          /* passed to clock and work               */
    uint64_t      t0_ns;        /* boundary time of slot 0                */
    uint32_t      slot_ns;      /* slot duration (from numerology)        */
    uint64_t      slots_done;   /* counter                                */
} slot_loop_t;

oru_status_t slot_loop_init(slot_loop_t *lp, datapath_t *dp,
                            slot_clock_fn clock, slot_work_fn work, void *ctx,
                            uint64_t t0_ns, uint32_t scs_hz);

/*
 * Run up to `max_slots` slots. For each slot, the loop computes the slot
 * boundary, (in a real build) waits until the clock reaches it, then calls
 * the work callback. Here it advances deterministically using the supplied
 * clock so tests can inject time. Stops early if work returns non-OK.
 * Returns the status of the last work call (ORU_OK if all succeeded).
 */
oru_status_t slot_loop_run(slot_loop_t *lp, uint64_t max_slots);

#endif /* ORU_SLOT_LOOP_H */
