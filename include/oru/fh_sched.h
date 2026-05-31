/* SPDX-License-Identifier: MIT */
/*
 * Fronthaul transmission-window scheduler (O-RAN.WG4.CUS-Plane timing).
 *
 * O-RAN defines reception/transmission windows so the O-RU knows, relative
 * to the PTP-synchronised slot boundary, when fronthaul packets are allowed
 * to arrive (DL) or must be sent (UL). This module models those windows so
 * the data path can be exercised and verified on a host, without real PTP
 * or radio hardware. See docs/03-oran-fronthaul-7.2x.md (timing).
 *
 * DL (downlink, DU -> O-RU) reception window for a U-plane packet whose
 * slot boundary is at time t_slot:
 *
 *     arrival allowed within   [ t_slot - T2a_max , t_slot - T2a_min ]
 *
 *   - too early  (arrival <  t_slot - T2a_max)  -> EARLY  (buffer overrun risk)
 *   - on time    (within the window)            -> ON_TIME
 *   - too late   (arrival >  t_slot - T2a_min)   -> LATE   (missed, dropped)
 *
 * UL (uplink, O-RU -> DU): the O-RU must transmit its IQ no later than
 * Ta3_max after the slot boundary (and no earlier than Ta3_min):
 *
 *     send due within          [ t_slot + Ta3_min , t_slot + Ta3_max ]
 *
 * All times are nanoseconds on the shared PTP timescale.
 */
#ifndef ORU_FH_SCHED_H
#define ORU_FH_SCHED_H

#include "oru/types.h"

typedef enum {
    FH_DL = 0,   /* downlink reception window  */
    FH_UL = 1,   /* uplink transmission window */
} fh_direction_t;

typedef enum {
    FH_EARLY = 0,   /* before the window: arrived/considered too soon  */
    FH_ON_TIME,     /* within the window                               */
    FH_LATE,        /* after the window: missed its deadline           */
} fh_window_result_t;

/* Window bounds, in nanoseconds, relative to the slot boundary.
 * For DL these are the T2a offsets; for UL the Ta3 offsets. */
typedef struct {
    uint32_t t2a_min_ns;   /* DL: earliest useful arrival before slot  */
    uint32_t t2a_max_ns;   /* DL: earliest allowed arrival before slot */
    uint32_t ta3_min_ns;   /* UL: earliest send after slot             */
    uint32_t ta3_max_ns;   /* UL: latest send after slot (deadline)    */
} fh_sched_cfg_t;

/* Running counters, separated by direction and result. */
typedef struct {
    uint64_t dl_early, dl_on_time, dl_late;
    uint64_t ul_early, ul_on_time, ul_late;
} fh_sched_stats_t;

typedef struct {
    fh_sched_cfg_t   cfg;
    uint32_t         slot_ns;     /* slot duration (derived from SCS)   */
    fh_sched_stats_t stats;
} fh_sched_t;

/* Initialise with a config and the numerology (subcarrier spacing in Hz,
 * e.g. 30000). slot_ns is derived from the SCS. */
oru_status_t fh_sched_init(fh_sched_t *s, const fh_sched_cfg_t *cfg,
                           uint32_t scs_hz);

/* Slot duration in ns for a given SCS (15/30/60/120 kHz). 0 on bad input. */
uint32_t fh_sched_slot_ns(uint32_t scs_hz);

/* The slot boundary time (ns) for slot index `slot` counted from t0. */
uint64_t fh_sched_slot_boundary(const fh_sched_t *s, uint64_t t0_ns,
                                uint64_t slot);

/* Classify a packet given the slot-boundary time and the actual event time
 * (DL: arrival time; UL: the time we are about to send). Updates stats. */
fh_window_result_t fh_sched_classify(fh_sched_t *s, fh_direction_t dir,
                                     uint64_t slot_boundary_ns,
                                     uint64_t event_ns);

const char *fh_window_result_str(fh_window_result_t r);

#endif /* ORU_FH_SCHED_H */
