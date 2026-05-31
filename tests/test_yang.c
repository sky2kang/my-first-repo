/* SPDX-License-Identifier: MIT */
/* Tests for YANG config validation and JSON instance-data export. */
#include "oru/yang.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static oru_carrier_cfg_t good_cfg(void)
{
    oru_carrier_cfg_t c = {
        .center_freq_hz = 3500000000ull,
        .bandwidth_hz = 100000000u,
        .scs_hz = 30000u,
        .num_tx = 4, .num_rx = 4,
    };
    snprintf(c.band, sizeof(c.band), "%s", "n78");
    snprintf(c.duplex, sizeof(c.duplex), "%s", "TDD");
    return c;
}

static void test_valid(void)
{
    oru_carrier_cfg_t c = good_cfg();
    char err[96];
    assert(yang_validate_carrier(&c, err, sizeof(err)) == ORU_OK);
}

static void test_invalid_cases(void)
{
    char err[96];
    oru_carrier_cfg_t c;

    c = good_cfg(); c.center_freq_hz = 100000000ull;  /* < FR1 */
    assert(yang_validate_carrier(&c, err, sizeof(err)) == ORU_ERR_PARAM);

    c = good_cfg(); c.bandwidth_hz = 200000000u;       /* > 100 MHz */
    assert(yang_validate_carrier(&c, err, sizeof(err)) == ORU_ERR_PARAM);

    c = good_cfg(); c.scs_hz = 7500u;                  /* invalid SCS */
    assert(yang_validate_carrier(&c, err, sizeof(err)) == ORU_ERR_PARAM);

    c = good_cfg(); c.num_tx = 5;                      /* > 4T */
    assert(yang_validate_carrier(&c, err, sizeof(err)) == ORU_ERR_PARAM);

    c = good_cfg(); snprintf(c.duplex, sizeof(c.duplex), "%s", "XDD");
    assert(yang_validate_carrier(&c, err, sizeof(err)) == ORU_ERR_PARAM);
}

static void test_json_export(void)
{
    oru_carrier_cfg_t c = good_cfg();
    char json[1024];
    int n = yang_carrier_to_json(&c, json, sizeof(json));
    assert(n > 0);
    assert((size_t)n == strlen(json));

    /* spot-check key leaves are present */
    assert(strstr(json, "o-ran-uplane-conf:user-plane-configuration"));
    assert(strstr(json, "tx-array-carriers"));
    assert(strstr(json, "rx-array-carriers"));
    assert(strstr(json, "\"absolute-frequency-center\": 3500000000"));
    assert(strstr(json, "\"subcarrier-spacing\": 30000"));
    assert(strstr(json, "\"channel-bandwidth\": 100000"));  /* kHz */
    assert(strstr(json, "\"n78-tx\""));
    assert(strstr(json, "\"duplex-scheme\": \"TDD\""));
}

static void test_json_buffer_too_small(void)
{
    oru_carrier_cfg_t c = good_cfg();
    char json[16];
    assert(yang_carrier_to_json(&c, json, sizeof(json)) < 0);
}

int main(void)
{
    test_valid();
    test_invalid_cases();
    test_json_export();
    test_json_buffer_too_small();
    printf("test_yang: PASS\n");
    return 0;
}
