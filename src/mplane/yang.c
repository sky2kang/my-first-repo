/* SPDX-License-Identifier: MIT */
#include "oru/yang.h"
#include "oru/log.h"

#include <stdio.h>
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
