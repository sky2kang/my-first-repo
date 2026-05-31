/* SPDX-License-Identifier: MIT */
/*
 * Minimal INI-style config loader.
 *
 * In a production O-RU these values come from the M-plane (NETCONF/YANG
 * datastore). Here we use a flat key=value file so the whole stack is
 * buildable and testable on a host without sysrepo. Keys are "section.key".
 */
#ifndef ORU_CONFIG_H
#define ORU_CONFIG_H

#include "oru/types.h"

typedef struct oru_config oru_config_t;

oru_config_t *oru_config_load(const char *path);
void          oru_config_free(oru_config_t *cfg);

/* Lookup helpers; return default if "section.key" is absent. */
const char *oru_config_get_str(const oru_config_t *cfg, const char *key,
                               const char *def);
long        oru_config_get_int(const oru_config_t *cfg, const char *key,
                               long def);

/* Convenience: fill a carrier config from the loaded file. */
oru_status_t oru_config_get_carrier(const oru_config_t *cfg,
                                    oru_carrier_cfg_t *out);

#endif /* ORU_CONFIG_H */
