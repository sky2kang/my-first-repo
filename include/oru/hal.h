/* SPDX-License-Identifier: MIT */
/*
 * Hardware Abstraction Layer for the RF front-end (ADRV9025 + JESD204).
 *
 * Upper layers (fronthaul / app) only ever talk to this interface. The
 * implementation is selected at build time:
 *   - HAL_SIM    (host build)   -> src/hal/adrv9025/*  with simulated HW
 *   - HAL_TARGET (Zynq build)   -> real ADI API / UIO / devmem access
 */
#ifndef ORU_HAL_H
#define ORU_HAL_H

#include "oru/types.h"

/* Lifecycle */
oru_status_t hal_init(const char *profile_path);
void         hal_shutdown(void);

/* JESD204 SoC<->ADRV9025 serial link bring-up (CGS -> ILAS -> DATA). */
oru_status_t hal_jesd204_bringup(void);
bool         hal_jesd204_is_locked(void);

/* RF control */
oru_status_t hal_set_carrier(uint64_t center_freq_hz);
oru_status_t hal_tx_enable(uint8_t chan_mask);
oru_status_t hal_rx_enable(uint8_t chan_mask);

/* Datapath (frequency-domain IQ for O-RAN 7.2x).
 * In the real system these push/pull from the PL digital front-end. */
oru_status_t hal_tx_iq(uint8_t chan, const oru_iq16_t *samples, size_t n);
oru_status_t hal_rx_iq(uint8_t chan, oru_iq16_t *samples, size_t n);

#endif /* ORU_HAL_H */
