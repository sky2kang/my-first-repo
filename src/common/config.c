/* SPDX-License-Identifier: MIT */
#include "oru/config.h"
#include "oru/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "config"
#define MAX_ENTRIES 128
#define MAX_KEY 96
#define MAX_VAL 128

struct kv {
    char key[MAX_KEY];   /* "section.key" */
    char val[MAX_VAL];
};

struct oru_config {
    struct kv entries[MAX_ENTRIES];
    size_t n;
};

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
                       end[-1] == '\r' || end[-1] == '\n'))
        *--end = '\0';
    return s;
}

oru_config_t *oru_config_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        LOGE(TAG, "cannot open config file: %s", path);
        return NULL;
    }

    oru_config_t *cfg = calloc(1, sizeof(*cfg));
    if (!cfg) {
        fclose(f);
        return NULL;
    }

    char line[256];
    char section[64] = "";
    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);
        if (*s == '\0' || *s == '#' || *s == ';')
            continue;

        if (*s == '[') {
            char *close = strchr(s, ']');
            if (close) {
                *close = '\0';
                snprintf(section, sizeof(section), "%s", trim(s + 1));
            }
            continue;
        }

        char *eq = strchr(s, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *k = trim(s);
        char *v = trim(eq + 1);

        if (cfg->n >= MAX_ENTRIES) {
            LOGW(TAG, "too many config entries, ignoring rest");
            break;
        }
        struct kv *e = &cfg->entries[cfg->n++];
        if (section[0])
            snprintf(e->key, sizeof(e->key), "%s.%s", section, k);
        else
            snprintf(e->key, sizeof(e->key), "%s", k);
        snprintf(e->val, sizeof(e->val), "%s", v);
    }

    fclose(f);
    LOGI(TAG, "loaded %zu entries from %s", cfg->n, path);
    return cfg;
}

void oru_config_free(oru_config_t *cfg)
{
    free(cfg);
}

const char *oru_config_get_str(const oru_config_t *cfg, const char *key,
                               const char *def)
{
    if (!cfg || !key)
        return def;
    for (size_t i = 0; i < cfg->n; i++) {
        if (strcmp(cfg->entries[i].key, key) == 0)
            return cfg->entries[i].val;
    }
    return def;
}

long oru_config_get_int(const oru_config_t *cfg, const char *key, long def)
{
    const char *v = oru_config_get_str(cfg, key, NULL);
    if (!v)
        return def;
    return strtol(v, NULL, 0);
}

oru_status_t oru_config_get_carrier(const oru_config_t *cfg,
                                    oru_carrier_cfg_t *out)
{
    if (!cfg || !out)
        return ORU_ERR_PARAM;

    memset(out, 0, sizeof(*out));
    out->center_freq_hz = (uint64_t)oru_config_get_int(cfg,
                              "carrier.center_freq_hz", 3500000000LL);
    out->bandwidth_hz   = (uint32_t)oru_config_get_int(cfg,
                              "carrier.bandwidth_hz", 100000000);
    out->scs_hz         = (uint32_t)oru_config_get_int(cfg,
                              "carrier.scs_hz", 30000);
    out->num_tx         = (uint8_t)oru_config_get_int(cfg, "array.num_tx", 4);
    out->num_rx         = (uint8_t)oru_config_get_int(cfg, "array.num_rx", 4);
    snprintf(out->band, sizeof(out->band), "%s",
             oru_config_get_str(cfg, "carrier.band", "n78"));
    snprintf(out->duplex, sizeof(out->duplex), "%s",
             oru_config_get_str(cfg, "carrier.duplex", "TDD"));
    return ORU_OK;
}
