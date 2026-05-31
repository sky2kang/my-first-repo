/* SPDX-License-Identifier: MIT */
/*
 * ADRV9025 HAL implementation.
 *
 * Two build flavors share this file:
 *   HAL_SIM    - host/simulation: no real hardware, deterministic stubs.
 *   HAL_TARGET - Zynq: TODO hooks where ADI adi_adrv9025_* API / SPI /
 *                UIO mappings are wired in.
 *
 * Keeping both behind one interface (oru/hal.h) means the entire upper
 * stack is identical on host and target.
 */
#include "oru/hal.h"
#include "oru/log.h"
#include "jesd204.h"

#include <string.h>

#define TAG "hal"

#ifdef HAL_TARGET
#  define HAL_MODE_STR "TARGET"
#else
#  define HAL_MODE_STR "SIM"
#endif

static struct {
    bool           initialized;
    uint64_t       carrier_hz;
    uint8_t        tx_mask;
    uint8_t        rx_mask;
    jesd204_link_t jesd;
} g_hal;

oru_status_t hal_init(const char *profile_path)
{
    memset(&g_hal, 0, sizeof(g_hal));
    g_hal.jesd.lanes      = 4;   /* L=4  */
    g_hal.jesd.converters = 8;   /* M=8 (4T4R I/Q) */

    LOGI(TAG, "ADRV9025 init (%s), profile=%s", HAL_MODE_STR,
         profile_path ? profile_path : "(none)");

#ifdef HAL_TARGET
    /* TODO(target):
     *   1. open SPI, read ADRV9025 product id register, verify
     *   2. adi_adrv9025_PreMcsInit() with the loaded profile/.bin
     *   3. multi-chip sync (MCS)
     *   4. adi_adrv9025_PostMcsInit()
     * Return ORU_ERR_HW on any failure. */
#else
    if (!profile_path)
        LOGW(TAG, "no profile path; using built-in SIM defaults");
#endif

    g_hal.initialized = true;
    return ORU_OK;
}

void hal_shutdown(void)
{
    if (!g_hal.initialized)
        return;
    LOGI(TAG, "shutdown");
    g_hal.initialized = false;
}

oru_status_t hal_jesd204_bringup(void)
{
    if (!g_hal.initialized)
        return ORU_ERR_HW;
    return jesd204_bringup(&g_hal.jesd);
}

bool hal_jesd204_is_locked(void)
{
    return g_hal.jesd.locked;
}

oru_status_t hal_set_carrier(uint64_t center_freq_hz)
{
    if (!g_hal.initialized)
        return ORU_ERR_HW;
    g_hal.carrier_hz = center_freq_hz;
    LOGI(TAG, "set carrier LO = %llu Hz",
         (unsigned long long)center_freq_hz);
#ifdef HAL_TARGET
    /* TODO(target): adi_adrv9025_RfPllFrequencySet(...) */
#endif
    return ORU_OK;
}

oru_status_t hal_tx_enable(uint8_t chan_mask)
{
    if (!g_hal.initialized)
        return ORU_ERR_HW;
    g_hal.tx_mask = chan_mask;
    LOGI(TAG, "TX enable mask=0x%02x", chan_mask);
    return ORU_OK;
}

oru_status_t hal_rx_enable(uint8_t chan_mask)
{
    if (!g_hal.initialized)
        return ORU_ERR_HW;
    g_hal.rx_mask = chan_mask;
    LOGI(TAG, "RX enable mask=0x%02x", chan_mask);
    return ORU_OK;
}

oru_status_t hal_tx_iq(uint8_t chan, const oru_iq16_t *samples, size_t n)
{
    if (!g_hal.initialized || !samples)
        return ORU_ERR_PARAM;
    if (!g_hal.jesd.locked)
        return ORU_ERR_HW;
#ifdef HAL_TARGET
    /* TODO(target): DMA samples into the PL digital front-end TX path. */
#else
    LOGT(TAG, "TX ch%u %zu samples (SIM, dropped)", chan, n);
#endif
    return ORU_OK;
}

oru_status_t hal_rx_iq(uint8_t chan, oru_iq16_t *samples, size_t n)
{
    if (!g_hal.initialized || !samples)
        return ORU_ERR_PARAM;
    if (!g_hal.jesd.locked)
        return ORU_ERR_HW;
#ifdef HAL_TARGET
    /* TODO(target): DMA samples out of the PL digital front-end RX path. */
#else
    /* SIM: produce a trivial deterministic tone-ish pattern. */
    for (size_t k = 0; k < n; k++) {
        samples[k].i = (int16_t)(k & 0x7f);
        samples[k].q = (int16_t)-(int16_t)(k & 0x7f);
    }
    LOGT(TAG, "RX ch%u %zu samples (SIM, synthetic)", chan, n);
#endif
    return ORU_OK;
}
