/* SPDX-License-Identifier: MIT */
#include "oru/fronthaul.h"
#include "oru/log.h"

#define TAG "fronthaul"

oru_status_t fronthaul_init(void)
{
    /* TODO(target): open the fronthaul Ethernet interface (raw socket /
     * AF_PACKET or DPDK), set up VLAN/PCP filters, and spin the U/C-plane
     * RX threads bound to the right CPU cores. In SIM we just announce. */
    LOGI(TAG, "eCPRI listener ready (U-plane/C-plane)");
    return ORU_OK;
}

void fronthaul_shutdown(void)
{
    LOGI(TAG, "shutdown");
}
