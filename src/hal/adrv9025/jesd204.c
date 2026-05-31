/* SPDX-License-Identifier: MIT */
#include "jesd204.h"
#include "oru/log.h"

#define TAG "jesd204"

const char *jesd204_state_str(jesd_state_t s)
{
    switch (s) {
    case JESD_RESET: return "RESET";
    case JESD_CGS:   return "CGS";
    case JESD_ILAS:  return "ILAS";
    case JESD_DATA:  return "DATA";
    default:         return "?";
    }
}

/*
 * Walk the JESD204 bring-up sequence. On HAL_SIM this just advances the
 * state machine and declares lock. On HAL_TARGET this is where you would
 * poke the Xilinx JESD204 IP registers, issue SYSREF, and poll the
 * deframer status until ILAS completes.
 */
oru_status_t jesd204_bringup(jesd204_link_t *link)
{
    if (!link)
        return ORU_ERR_PARAM;

    static const jesd_state_t seq[] = { JESD_CGS, JESD_ILAS, JESD_DATA };
    for (unsigned i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
        link->state = seq[i];
        LOGD(TAG, "link state -> %s", jesd204_state_str(link->state));
#ifdef HAL_TARGET
        /* TODO(target): poll IP status register here, return ORU_ERR_TIMEOUT
         * if the stage does not complete within the deadline. */
#endif
    }

    link->locked = (link->state == JESD_DATA);
    LOGI(TAG, "bring-up done: L=%u M=%u state=%s lock=%d",
         link->lanes, link->converters, jesd204_state_str(link->state),
         link->locked);
    return link->locked ? ORU_OK : ORU_ERR_HW;
}
