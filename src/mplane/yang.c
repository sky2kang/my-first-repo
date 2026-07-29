/* SPDX-License-Identifier: MIT */
#include "oru/yang.h"
#include "oru/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "yang"

/* SCS values allowed by 5G NR numerologies (Hz). */
static int scs_is_valid(uint32_t scs_hz)
{
    return scs_hz == 15000 || scs_hz == 30000 ||
           scs_hz == 60000 || scs_hz == 120000;
}

static void set_err(char *err, size_t len, const char *msg)
{
    if (err && len)
        snprintf(err, len, "%s", msg);
}

oru_status_t yang_validate_carrier(const oru_carrier_cfg_t *cfg,
                                   char *err, size_t err_len)
{
    if (!cfg) {
        set_err(err, err_len, "null config");
        return ORU_ERR_PARAM;
    }

    /* o-ran constraints: center frequency in the FR1 range we target. */
    if (cfg->center_freq_hz < 400000000ull ||
        cfg->center_freq_hz > 7125000000ull) {
        set_err(err, err_len, "center-freq out of FR1 range (0.41-7.125 GHz)");
        return ORU_ERR_PARAM;
    }
    /* channel bandwidth: NR FR1 carrier BW up to 100 MHz. */
    if (cfg->bandwidth_hz < 5000000u || cfg->bandwidth_hz > 100000000u) {
        set_err(err, err_len, "bandwidth out of range (5-100 MHz)");
        return ORU_ERR_PARAM;
    }
    if (!scs_is_valid(cfg->scs_hz)) {
        set_err(err, err_len, "invalid subcarrier spacing");
        return ORU_ERR_PARAM;
    }
    /* ADRV9025 is 4T4R: at least one, at most four chains. */
    if (cfg->num_tx < 1 || cfg->num_tx > 4) {
        set_err(err, err_len, "num_tx out of range (1-4)");
        return ORU_ERR_PARAM;
    }
    if (cfg->num_rx < 1 || cfg->num_rx > 4) {
        set_err(err, err_len, "num_rx out of range (1-4)");
        return ORU_ERR_PARAM;
    }
    if (strcmp(cfg->duplex, "TDD") != 0 && strcmp(cfg->duplex, "FDD") != 0) {
        set_err(err, err_len, "duplex must be TDD or FDD");
        return ORU_ERR_PARAM;
    }

    set_err(err, err_len, "ok");
    return ORU_OK;
}

int yang_carrier_to_json(const oru_carrier_cfg_t *cfg, char *buf,
                         size_t buf_len)
{
    if (!cfg || !buf)
        return ORU_ERR_PARAM;

    /* Shaped after o-ran-uplane-conf instance data. Frequencies in Hz,
     * bandwidth in kHz to match the YANG leaf units. */
    int n = snprintf(buf, buf_len,
        "{\n"
        "  \"o-ran-uplane-conf:user-plane-configuration\": {\n"
        "    \"tx-array-carriers\": [\n"
        "      {\n"
        "        \"name\": \"%s-tx\",\n"
        "        \"absolute-frequency-center\": %llu,\n"
        "        \"channel-bandwidth\": %u,\n"
        "        \"subcarrier-spacing\": %u,\n"
        "        \"duplex-scheme\": \"%s\",\n"
        "        \"active\": \"ACTIVE\",\n"
        "        \"number-of-antennas\": %u\n"
        "      }\n"
        "    ],\n"
        "    \"rx-array-carriers\": [\n"
        "      {\n"
        "        \"name\": \"%s-rx\",\n"
        "        \"absolute-frequency-center\": %llu,\n"
        "        \"channel-bandwidth\": %u,\n"
        "        \"subcarrier-spacing\": %u,\n"
        "        \"number-of-antennas\": %u\n"
        "      }\n"
        "    ]\n"
        "  }\n"
        "}\n",
        cfg->band,
        (unsigned long long)cfg->center_freq_hz,
        cfg->bandwidth_hz / 1000u,         /* kHz */
        cfg->scs_hz,
        cfg->duplex,
        cfg->num_tx,
        cfg->band,
        (unsigned long long)cfg->center_freq_hz,
        cfg->bandwidth_hz / 1000u,
        cfg->scs_hz,
        cfg->num_rx);

    if (n < 0)
        return ORU_ERR;
    if ((size_t)n >= buf_len) {
        LOGE(TAG, "JSON buffer too small (need %d, have %zu)", n + 1, buf_len);
        return ORU_ERR_PARAM;
    }
    return n;
}

/* --- Minimal JSON leaf extraction (dependency-free) ---------------------- */

/* Find the value text following the first occurrence of "key" in `json`.
 * Returns a pointer just past the ':' (skipping whitespace), or NULL. */
static const char *find_value(const char *json, const char *key)
{
    char needle[64];
    int kn = snprintf(needle, sizeof(needle), "\"%s\"", key);
    if (kn < 0 || (size_t)kn >= sizeof(needle))
        return NULL;

    const char *p = strstr(json, needle);
    if (!p)
        return NULL;
    p = strchr(p + kn, ':');
    if (!p)
        return NULL;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    return p;
}

/* Read an unsigned long long leaf. Returns false if absent / not numeric. */
static bool get_u64(const char *json, const char *key, uint64_t *out)
{
    const char *p = find_value(json, key);
    if (!p || *p < '0' || *p > '9')
        return false;
    *out = strtoull(p, NULL, 10);
    return true;
}

/* Read a quoted string leaf into dst (truncated to dst_len). */
static bool get_str(const char *json, const char *key, char *dst,
                    size_t dst_len)
{
    const char *p = find_value(json, key);
    if (!p || *p != '"')
        return false;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < dst_len)
        dst[i++] = *p++;
    dst[i] = '\0';
    return *p == '"';
}

oru_status_t yang_carrier_from_json(const char *json, oru_carrier_cfg_t *out,
                                    char *err, size_t err_len)
{
    if (!json || !out)
        return ORU_ERR_PARAM;

    memset(out, 0, sizeof(*out));

    uint64_t freq = 0, bw_khz = 0, scs = 0, ntx = 0;
    if (!get_u64(json, "absolute-frequency-center", &freq)) {
        set_err(err, err_len, "missing absolute-frequency-center");
        return ORU_ERR_PROTO;
    }
    if (!get_u64(json, "channel-bandwidth", &bw_khz)) {
        set_err(err, err_len, "missing channel-bandwidth");
        return ORU_ERR_PROTO;
    }
    if (!get_u64(json, "subcarrier-spacing", &scs)) {
        set_err(err, err_len, "missing subcarrier-spacing");
        return ORU_ERR_PROTO;
    }
    if (!get_u64(json, "number-of-antennas", &ntx)) {
        set_err(err, err_len, "missing number-of-antennas");
        return ORU_ERR_PROTO;
    }

    out->center_freq_hz = freq;
    out->bandwidth_hz   = (uint32_t)(bw_khz * 1000u);   /* kHz -> Hz */
    out->scs_hz         = (uint32_t)scs;
    out->num_tx         = (uint8_t)ntx;
    out->num_rx         = (uint8_t)ntx;   /* tx/rx symmetric in our export */

    /* duplex-scheme (optional, default TDD) */
    if (!get_str(json, "duplex-scheme", out->duplex, sizeof(out->duplex)))
        snprintf(out->duplex, sizeof(out->duplex), "%s", "TDD");

    /* carrier name "<band>-tx" -> band; default n78 if unparseable.
     * Read straight into the band field (band names are short). */
    if (get_str(json, "name", out->band, sizeof(out->band))) {
        char *dash = strrchr(out->band, '-');
        if (dash)
            *dash = '\0';
    } else {
        snprintf(out->band, sizeof(out->band), "%s", "n78");
    }

    /* Guarantee the parsed result is valid before handing it back. */
    return yang_validate_carrier(out, err, err_len);
}
