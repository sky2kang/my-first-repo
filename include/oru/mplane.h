/* SPDX-License-Identifier: MIT */
/* M-plane: NETCONF/YANG management (see docs/05-mplane-splane.md). */
#ifndef ORU_MPLANE_H
#define ORU_MPLANE_H

#include "oru/types.h"
#include "oru/config.h"

oru_status_t mplane_init(void);
void         mplane_shutdown(void);

/*
 * Apply configuration to the radio. In production this is driven by
 * NETCONF edit-config callbacks; here we derive it from the config file
 * to keep the same code path testable on a host.
 */
oru_status_t mplane_apply_config(const oru_config_t *cfg,
                                 oru_carrier_cfg_t *out_carrier);

/* Report a fault/alarm upstream (o-ran-fm). Stubbed to a log for now. */
void mplane_raise_alarm(const char *fault_id, const char *text);

#endif /* ORU_MPLANE_H */
