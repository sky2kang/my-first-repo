/* SPDX-License-Identifier: MIT */
#include "oru/splane.h"
#include "oru/log.h"

#define TAG "splane"

static bool g_locked;

oru_status_t splane_init(void)
{
    g_locked = false;
    LOGI(TAG, "init (IEEE 1588 PTP)");
    return ORU_OK;
}

void splane_shutdown(void)
{
    g_locked = false;
}

oru_status_t splane_wait_lock(int timeout_ms)
{
#ifdef HAL_TARGET
    /* TODO(target): poll ptp4l (e.g. via the management socket or
     * `pmc` GET PORT_DATA_SET) until portState == SLAVE and the servo is
     * locked. Honour timeout_ms; return ORU_ERR_TIMEOUT otherwise. */
    (void)timeout_ms;
    LOGW(TAG, "PTP polling not implemented on target; assuming locked");
    g_locked = true;
    return ORU_OK;
#else
    /* SIM: lock is instantaneous. */
    (void)timeout_ms;
    LOGI(TAG, "PTP waiting for lock (SIM) ... LOCKED");
    g_locked = true;
    return ORU_OK;
#endif
}

bool splane_is_locked(void)
{
    return g_locked;
}
