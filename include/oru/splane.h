/* SPDX-License-Identifier: MIT */
/* S-plane: IEEE 1588 PTP synchronization (see docs/05-mplane-splane.md). */
#ifndef ORU_SPLANE_H
#define ORU_SPLANE_H

#include "oru/types.h"

oru_status_t splane_init(void);
void         splane_shutdown(void);

/* Block until PTP achieves lock or timeout elapses.
 * HAL_SIM returns immediately as LOCKED; target polls ptp4l. */
oru_status_t splane_wait_lock(int timeout_ms);
bool         splane_is_locked(void);

#endif /* ORU_SPLANE_H */
