/* SPDX-License-Identifier: MIT */
/* Internal JESD204 link bring-up state (see docs/04-adrv9025-jesd204.md). */
#ifndef ORU_HAL_JESD204_H
#define ORU_HAL_JESD204_H

#include "oru/types.h"

typedef enum {
    JESD_RESET = 0,
    JESD_CGS,     /* code group sync          */
    JESD_ILAS,    /* initial lane alignment   */
    JESD_DATA,    /* user data / locked       */
} jesd_state_t;

typedef struct {
    uint8_t      lanes;     /* L */
    uint8_t      converters;/* M */
    jesd_state_t state;
    bool         locked;
} jesd204_link_t;

oru_status_t jesd204_bringup(jesd204_link_t *link);
const char  *jesd204_state_str(jesd_state_t s);

#endif /* ORU_HAL_JESD204_H */
