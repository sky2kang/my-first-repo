/* SPDX-License-Identifier: MIT */
#include "oru/perf.h"
#include "oru/log.h"

#include <stdio.h>
#include <string.h>

#define TAG "perf"

void perf_init(perf_t *p)
{
    if (p)
        memset(p, 0, sizeof(*p));
}

void perf_rx_packet(perf_t *p, size_t bytes)
{
    if (!p) return;
    p->live.rx_packets++;
    p->live.rx_bytes += bytes;
}

void perf_tx_packet(perf_t *p, size_t bytes)
{
    if (!p) return;
    p->live.tx_packets++;
    p->live.tx_bytes += bytes;
}

void perf_seq_gap(perf_t *p)
{
    if (p) p->live.seq_gaps++;
}

void perf_dl_window(perf_t *p, int on_time, int early, int late)
{
    if (!p) return;
    if (on_time) p->live.dl_on_time++;
    if (early)   p->live.dl_early++;
    if (late)    p->live.dl_late++;
}

void perf_ul_window(perf_t *p, int on_time, int late)
{
    if (!p) return;
    if (on_time) p->live.ul_on_time++;
    if (late)    p->live.ul_late++;
}

void perf_cu_result(perf_t *p, int matched, int orphan, int out_of_range)
{
    if (!p) return;
    if (matched)      p->live.cu_matched++;
    if (orphan)       p->live.cu_orphan++;
    if (out_of_range) p->live.cu_out_of_range++;
}

void perf_evm_sample(perf_t *p, double evm_pct)
{
    if (!p) return;
    if (evm_pct < 0.0) evm_pct = 0.0;
    p->live.evm_pct_x100_sum += (uint64_t)(evm_pct * 100.0 + 0.5);
    p->live.evm_samples++;
}

double perf_mean_evm_pct(const perf_t *p)
{
    if (!p || p->live.evm_samples == 0)
        return 0.0;
    return (double)p->live.evm_pct_x100_sum / (double)p->live.evm_samples
           / 100.0;
}

void perf_snapshot(perf_t *p)
{
    if (!p) return;
    p->last = p->live;
    p->interval_id++;
    memset(&p->live, 0, sizeof(p->live));
    LOGI(TAG, "interval %llu closed: rx=%llu tx=%llu dl_late=%llu "
              "ul_late=%llu orphan=%llu",
         (unsigned long long)p->interval_id,
         (unsigned long long)p->last.rx_packets,
         (unsigned long long)p->last.tx_packets,
         (unsigned long long)p->last.dl_late,
         (unsigned long long)p->last.ul_late,
         (unsigned long long)p->last.cu_orphan);
}

int perf_to_json(const perf_t *p, char *buf, size_t buf_len)
{
    if (!p || !buf)
        return ORU_ERR_PARAM;

    const perf_counters_t *c = &p->last;
    double mean_evm =
        (c->evm_samples ? (double)c->evm_pct_x100_sum
                          / (double)c->evm_samples / 100.0
                        : 0.0);

    int n = snprintf(buf, buf_len,
        "{\n"
        "  \"o-ran-performance-management:performance-measurement\": {\n"
        "    \"interval-id\": %llu,\n"
        "    \"transport-measured-result\": {\n"
        "      \"rx-packets\": %llu,\n"
        "      \"tx-packets\": %llu,\n"
        "      \"rx-bytes\": %llu,\n"
        "      \"tx-bytes\": %llu,\n"
        "      \"sequence-gaps\": %llu\n"
        "    },\n"
        "    \"timing-measured-result\": {\n"
        "      \"dl-on-time\": %llu,\n"
        "      \"dl-early\": %llu,\n"
        "      \"dl-late\": %llu,\n"
        "      \"ul-on-time\": %llu,\n"
        "      \"ul-late\": %llu\n"
        "    },\n"
        "    \"cuplane-measured-result\": {\n"
        "      \"matched\": %llu,\n"
        "      \"orphan\": %llu,\n"
        "      \"out-of-range\": %llu\n"
        "    },\n"
        "    \"radio-measured-result\": {\n"
        "      \"mean-evm-percent\": %.2f\n"
        "    }\n"
        "  }\n"
        "}\n",
        (unsigned long long)p->interval_id,
        (unsigned long long)c->rx_packets,
        (unsigned long long)c->tx_packets,
        (unsigned long long)c->rx_bytes,
        (unsigned long long)c->tx_bytes,
        (unsigned long long)c->seq_gaps,
        (unsigned long long)c->dl_on_time,
        (unsigned long long)c->dl_early,
        (unsigned long long)c->dl_late,
        (unsigned long long)c->ul_on_time,
        (unsigned long long)c->ul_late,
        (unsigned long long)c->cu_matched,
        (unsigned long long)c->cu_orphan,
        (unsigned long long)c->cu_out_of_range,
        mean_evm);

    if (n < 0)
        return ORU_ERR;
    if ((size_t)n >= buf_len) {
        LOGE(TAG, "PM JSON buffer too small (need %d, have %zu)",
             n + 1, buf_len);
        return ORU_ERR_PARAM;
    }
    return n;
}
