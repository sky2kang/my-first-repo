/* SPDX-License-Identifier: MIT */
/*
 * YANG instance-data representation and validation for the O-RU config.
 *
 * On a real O-RU the M-plane carries configuration as YANG instance data
 * (e.g. the o-ran-uplane-conf module) over NETCONF. Here we provide:
 *   - validation of a carrier config against the constraints those YANG
 *     models impose (value ranges, enums), and
 *   - serialization to JSON instance data shaped like the YANG tree,
 *
 * so the same values used by the running stack can be exported/inspected
 * in the canonical M-plane form without pulling in a full NETCONF server.
 */
#ifndef ORU_YANG_H
#define ORU_YANG_H

#include "oru/types.h"

/* Validate a carrier config against O-RAN/YANG constraints.
 * On failure returns ORU_ERR_PARAM and, if `err`/`err_len` are provided,
 * writes a human-readable reason. */
oru_status_t yang_validate_carrier(const oru_carrier_cfg_t *cfg,
                                    char *err, size_t err_len);

/*
 * Serialize a carrier config to JSON instance data shaped after
 * o-ran-uplane-conf (tx-array-carriers / rx-array-carriers). Writes a
 * NUL-terminated string into `buf`. Returns the number of bytes written
 * (excluding the NUL), or a negative oru_status_t (e.g. if the buffer is
 * too small). */
int yang_carrier_to_json(const oru_carrier_cfg_t *cfg,
                         char *buf, size_t buf_len);

#endif /* ORU_YANG_H */
