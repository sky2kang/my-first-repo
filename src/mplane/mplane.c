/* SPDX-License-Identifier: MIT */
#include "oru/mplane.h"
#include "oru/log.h"

#define TAG "mplane"

oru_status_t mplane_init(void)
{
    /* TODO(target): connect to sysrepo, subscribe to O-RAN YANG modules
     * (o-ran-uplane-conf, o-ran-module-cap, o-ran-fm, ...), and register
     * edit-config change callbacks that call mplane_apply_config(). */
    LOGI(TAG, "init (NETCONF/YANG management)");
    return ORU_OK;
}

void mplane_shutdown(void)
{
    LOGI(TAG, "shutdown");
}

oru_status_t mplane_apply_config(const oru_config_t *cfg,
                                 oru_carrier_cfg_t *out_carrier)
{
    if (!cfg || !out_carrier)
        return ORU_ERR_PARAM;

    oru_status_t rc = oru_config_get_carrier(cfg, out_carrier);
    if (rc != ORU_OK)
        return rc;

    LOGI(TAG, "applying config: band=%s bw=%uMHz scs=%ukHz %s tx=%u rx=%u",
         out_carrier->band,
         out_carrier->bandwidth_hz / 1000000u,
         out_carrier->scs_hz / 1000u,
         out_carrier->duplex,
         out_carrier->num_tx, out_carrier->num_rx);
    return ORU_OK;
}

void mplane_raise_alarm(const char *fault_id, const char *text)
{
    /* TODO(target): push an o-ran-fm alarm notification upstream. */
    LOGW(TAG, "ALARM %s: %s", fault_id ? fault_id : "?", text ? text : "");
}
