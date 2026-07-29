/* SPDX-License-Identifier: MIT */
/*
 * Common types shared across the O-RU software stack.
 */
#ifndef ORU_TYPES_H
#define ORU_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Return codes used everywhere. 0 == success, negative == error. */
typedef enum {
    ORU_OK            =  0,
    ORU_ERR          = -1,
    ORU_ERR_PARAM    = -2,   /* invalid argument                 */
    ORU_ERR_HW       = -3,   /* hardware / driver failure        */
    ORU_ERR_TIMEOUT  = -4,   /* operation timed out              */
    ORU_ERR_PROTO    = -5,   /* protocol / parse error           */
    ORU_ERR_NOTSUP   = -6,   /* not supported / not implemented  */
} oru_status_t;

/* O-RU top-level state machine (see docs/01-architecture.md §5). */
typedef enum {
    ORU_STATE_INIT = 0,
    ORU_STATE_SYNC,
    ORU_STATE_CONFIG,
    ORU_STATE_OPERATIONAL,
    ORU_STATE_FAULT,
} oru_state_t;

const char *oru_state_str(oru_state_t s);
const char *oru_status_str(oru_status_t s);

/* A single complex baseband sample (frequency-domain IQ for 7.2x). */
typedef struct {
    int16_t i;
    int16_t q;
} oru_iq16_t;

/* Carrier / radio configuration (subset; normally sourced from YANG). */
typedef struct {
    uint64_t center_freq_hz;   /* LO / carrier center frequency        */
    uint32_t bandwidth_hz;     /* channel bandwidth                    */
    uint32_t scs_hz;           /* subcarrier spacing (15/30/60 kHz)    */
    uint8_t  num_tx;           /* active TX chains (ADRV9025 up to 4)   */
    uint8_t  num_rx;           /* active RX chains (ADRV9025 up to 4)   */
    char     band[16];         /* e.g. "n78"                           */
    char     duplex[8];        /* "TDD" / "FDD"                        */
} oru_carrier_cfg_t;

#endif /* ORU_TYPES_H */
