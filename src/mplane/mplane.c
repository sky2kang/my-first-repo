/* SPDX-License-Identifier: MIT */
#include "oru/mplane.h"
#include "oru/yang.h"
#include "oru/log.h"

#define TAG "mplane"

static fm_t g_fm;

fm_t *mplane_fm(void)
{
    return &g_fm;
}

oru_status_t mplane_init(void)
{
    /* TODO(target): connect to sysrepo, subscribe to O-RAN YANG modules
     * (o-ran-uplane-conf, o-ran-module-cap, o-ran-fm, ...), and register
     * edit-config change callbacks that call mplane_apply_config(). */
    fm_init(&g_fm);
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

    /* Validate against the O-RAN/YANG constraints before applying, just as
     * a NETCONF server would reject an invalid edit-config. */
    char err[96];
    rc = yang_validate_carrier(out_carrier, err, sizeof(err));
    if (rc != ORU_OK) {
        LOGE(TAG, "config rejected by YANG validation: %s", err);
        return rc;
    }

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
    /* Record a MAJOR alarm in the FM store (which logs the raise). On the
     * target the FM layer also pushes an o-ran-fm notification upstream. */
    fm_raise(&g_fm, fault_id ? fault_id : "UNKNOWN", FM_MAJOR, text);
}
