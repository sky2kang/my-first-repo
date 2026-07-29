/* SPDX-License-Identifier: MIT */
/* Tests for performance-management counter aggregation (oru/perf.h). */
#include "oru/perf.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void test_accumulate(void)
{
    perf_t p;
    perf_init(&p);

    perf_rx_packet(&p, 100);
    perf_rx_packet(&p, 140);
    perf_tx_packet(&p, 200);
    perf_seq_gap(&p);

    assert(p.live.rx_packets == 2);
    assert(p.live.rx_bytes == 240);
    assert(p.live.tx_packets == 1);
    assert(p.live.tx_bytes == 200);
    assert(p.live.seq_gaps == 1);
}

static void test_window_and_cu(void)
{
    perf_t p;
    perf_init(&p);

    perf_dl_window(&p, 1, 0, 0);   /* on time */
    perf_dl_window(&p, 0, 0, 1);   /* late */
    perf_ul_window(&p, 1, 0);
    perf_ul_window(&p, 0, 1);

    perf_cu_result(&p, 1, 0, 0);   /* matched */
    perf_cu_result(&p, 0, 1, 0);   /* orphan */

    assert(p.live.dl_on_time == 1 && p.live.dl_late == 1);
    assert(p.live.ul_on_time == 1 && p.live.ul_late == 1);
    assert(p.live.cu_matched == 1 && p.live.cu_orphan == 1);
}

static void test_evm_mean(void)
{
    perf_t p;
    perf_init(&p);
    perf_evm_sample(&p, 2.0);
    perf_evm_sample(&p, 4.0);
    perf_evm_sample(&p, 3.0);
    assert(fabs(perf_mean_evm_pct(&p) - 3.0) < 0.01);

    perf_t empty;
    perf_init(&empty);
    assert(perf_mean_evm_pct(&empty) == 0.0);
}

static void test_snapshot_resets_live(void)
{
    perf_t p;
    perf_init(&p);

    perf_rx_packet(&p, 50);
    perf_dl_window(&p, 0, 0, 1);
    perf_snapshot(&p);

    /* last holds the closed interval; live is zeroed; id bumped */
    assert(p.interval_id == 1);
    assert(p.last.rx_packets == 1 && p.last.dl_late == 1);
    assert(p.live.rx_packets == 0 && p.live.dl_late == 0);

    /* a new interval accumulates independently */
    perf_rx_packet(&p, 10);
    perf_snapshot(&p);
    assert(p.interval_id == 2);
    assert(p.last.rx_packets == 1);   /* only the one from interval 2 */
}

static void test_json_export(void)
{
    perf_t p;
    perf_init(&p);
    perf_rx_packet(&p, 256);
    perf_tx_packet(&p, 512);
    perf_dl_window(&p, 1, 0, 0);
    perf_cu_result(&p, 1, 0, 0);
    perf_evm_sample(&p, 1.5);
    perf_snapshot(&p);

    char json[2048];
    int n = perf_to_json(&p, json, sizeof(json));
    assert(n > 0);
    assert((size_t)n == strlen(json));

    assert(strstr(json, "o-ran-performance-management:performance-measurement"));
    assert(strstr(json, "\"interval-id\": 1"));
    assert(strstr(json, "\"rx-packets\": 1"));
    assert(strstr(json, "\"tx-bytes\": 512"));
    assert(strstr(json, "\"dl-on-time\": 1"));
    assert(strstr(json, "\"matched\": 1"));
    assert(strstr(json, "\"mean-evm-percent\": 1.50"));
}

static void test_json_buffer_too_small(void)
{
    perf_t p;
    perf_init(&p);
    perf_snapshot(&p);
    char json[16];
    assert(perf_to_json(&p, json, sizeof(json)) < 0);
}

int main(void)
{
    test_accumulate();
    test_window_and_cu();
    test_evm_mean();
    test_snapshot_resets_live();
    test_json_export();
    test_json_buffer_too_small();
    printf("test_perf: PASS\n");
    return 0;
}
