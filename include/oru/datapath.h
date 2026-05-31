/* SPDX-License-Identifier: MIT */
/*
 * Fronthaul datapath: ties the timing-window scheduler (oru/fh_sched.h) to
 * the RF HAL (oru/hal.h) and the U-plane codec (oru/fronthaul.h), with
 * transmission-window deadline enforcement.
 *
 * Downlink (DU -> O-RU -> antenna):
 *   1. a U-plane packet arrives at time `arrival_ns` for a target slot;
 *   2. the scheduler classifies it against the DL reception (T2a) window;
 *   3. ON_TIME / EARLY packets are decoded and pushed to hal_tx_iq();
 *      LATE packets are dropped (the slot has already passed) and counted.
 *
 * Uplink (antenna -> O-RU -> DU):
 *   1. the O-RU pulls IQ from hal_rx_iq() and builds a U-plane packet;
 *   2. the scheduler checks the intended send time against the UL (Ta3)
 *      window; a packet past its deadline is still emitted but flagged as a
 *      late transmission for fault reporting.
 *
 * This keeps the deadline logic testable on a host (HAL_SIM) while mapping
 * cleanly onto a real RX/TX thread on the target.
 */
#ifndef ORU_DATAPATH_H
#define ORU_DATAPATH_H

#include "oru/types.h"
#include "oru/fronthaul.h"
#include "oru/fh_sched.h"

typedef struct {
    uint64_t dl_delivered;   /* packets decoded and sent to TX           */
    uint64_t dl_dropped_late;/* DL packets that missed the slot          */
    uint64_t ul_sent;        /* UL packets emitted                       */
    uint64_t ul_late;        /* UL packets emitted past their deadline   */
} datapath_stats_t;

typedef struct {
    fh_sched_t       sched;
    datapath_stats_t stats;
    uint8_t          tx_chan;
    uint8_t          rx_chan;
} datapath_t;

/* Initialise with timing windows and the numerology; binds TX/RX channels. */
oru_status_t datapath_init(datapath_t *dp, const fh_sched_cfg_t *cfg,
                           uint32_t scs_hz, uint8_t tx_chan, uint8_t rx_chan);

/*
 * Handle one inbound DL U-plane packet.
 *   buf/len      - the encoded U-plane packet (single section)
 *   slot_boundary_ns / arrival_ns - timing on the PTP timescale
 * Decodes and forwards to hal_tx_iq() unless the packet is LATE (dropped).
 * Returns ORU_OK if delivered, ORU_ERR_TIMEOUT if dropped as late, or a
 * negative status on decode/HW error.
 */
oru_status_t datapath_handle_dl(datapath_t *dp, const uint8_t *buf,
                                size_t len, uint64_t slot_boundary_ns,
                                uint64_t arrival_ns);

/*
 * Produce one outbound UL U-plane packet from captured RX IQ.
 *   num_prb      - PRBs to capture/encode
 *   comp_meth/iq_width - U-plane compression for the outbound packet
 *   slot_boundary_ns / send_ns - timing on the PTP timescale
 *   buf/len      - output buffer for the encoded packet
 * Returns bytes written (>=0). Sets *was_late if past the Ta3 deadline.
 */
int datapath_build_ul(datapath_t *dp, uint16_t num_prb, uint8_t comp_meth,
                      uint8_t iq_width, uint64_t slot_boundary_ns,
                      uint64_t send_ns, uint8_t *buf, size_t len,
                      bool *was_late);

#endif /* ORU_DATAPATH_H */
