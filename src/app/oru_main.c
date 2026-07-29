/* SPDX-License-Identifier: MIT */
/*
 * O-RU application entry point.
 *
 * Drives the top-level state machine (docs/01-architecture.md §5):
 *   INIT -> SYNC -> CONFIG -> OPERATIONAL  (-> FAULT on error)
 * and orchestrates the M/S/C/U planes plus the RF HAL.
 */
#include "oru/version.h"
#include "oru/types.h"
#include "oru/log.h"
#include "oru/config.h"
#include "oru/hal.h"
#include "oru/splane.h"
#include "oru/mplane.h"
#include "oru/fronthaul.h"
#include "oru/fh_sched.h"
#include "oru/datapath.h"
#include "oru/slot_loop.h"
#include "oru/comp_bench.h"
#include "oru/fh_packet.h"
#include "oru/cu_match.h"
#include "oru/prach.h"
#include "oru/beamform.h"
#include "oru/cfr.h"
#include "oru/dpd.h"
#include "oru/perf.h"
#include "oru/fm.h"
#include "oru/yang.h"

#include <math.h>

#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define TAG "oru_app"

static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static uint8_t mask_for(uint8_t n)
{
    return (uint8_t)((n >= 8) ? 0xff : ((1u << n) - 1u));
}

static const char *opt_value(int argc, char **argv, const char *flag,
                             const char *def)
{
    for (int i = 1; i < argc - 1; i++)
        if (strcmp(argv[i], flag) == 0)
            return argv[i + 1];
    return def;
}

/*
 * Print a compression comparison table over a synthetic signal: for each
 * method/width, the ratio vs raw 16-bit IQ and the round-trip RMSE. Handy
 * for picking an iqWidth without any hardware.
 */
static int run_comp_benchmark(void)
{
    enum { NPRB = 32, NRE = NPRB * 12 };
    static oru_iq16_t iq[NRE];
    for (int k = 0; k < NRE; k++) {
        iq[k].i = (int16_t)(8000.0 * sin(0.05 * k));
        iq[k].q = (int16_t)(8000.0 * cos(0.05 * k));
    }

    const struct { uint8_t meth; uint8_t width; const char *name; } cases[] = {
        { ORAN_COMP_NONE,       16, "none"       },
        { ORAN_COMP_BFP,        12, "bfp-12"     },
        { ORAN_COMP_BFP,         9, "bfp-9"      },
        { ORAN_COMP_MULAW,       9, "mulaw-9"    },
        { ORAN_COMP_MULAW,       8, "mulaw-8"    },
        { ORAN_COMP_MODULATION,  6, "modcomp-64qam" },
        { ORAN_COMP_MODULATION,  4, "modcomp-16qam" },
    };

    printf("%-16s %8s %8s %6s %8s\n",
           "method", "raw", "comp", "ratio", "rmse");
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        comp_bench_result_t r;
        if (comp_bench_run(iq, NPRB, cases[i].meth, cases[i].width, &r)
            != ORU_OK) {
            printf("%-16s  (failed)\n", cases[i].name);
            continue;
        }
        printf("%-16s %8zu %8zu %6.2f %8.1f\n",
               cases[i].name, r.raw_bytes, r.comp_bytes, r.ratio, r.rmse);
    }
    return EXIT_SUCCESS;
}

/*
 * End-to-end fronthaul packet demo (no radio): a DU side builds a C-plane
 * grant packet and a matching U-plane packet over eCPRI; the O-RU side
 * parses both, registers the grant, and matches the U-plane against it.
 * Exercises ecpri + C/U-plane + sequence tracking + cu_match together.
 */
static int run_fh_demo(void)
{
    const uint16_t eaxc = 0x0001;
    fh_tx_t tx; fh_tx_init(&tx);
    fh_rx_t rx; fh_rx_init(&rx);
    cu_matcher_t m; cu_match_init(&m);

    /* DU: C-plane grant for frame 1, slot 0, sym 0, PRBs [0,4). */
    oran_cplane_section_t grant = {
        .frame_id = 1, .slot_id = 0, .start_symbol_id = 0,
        .start_prb = 0, .num_prb = 4, .beam_id = 1,
    };
    uint8_t cpkt[256];
    int cn = fh_build_cplane_s1(&tx, eaxc, &grant, cpkt, sizeof(cpkt));

    /* DU: U-plane carrying IQ for those 4 PRBs (BFP-9). */
    oru_iq16_t iq[4 * 12];
    for (size_t k = 0; k < sizeof(iq) / sizeof(iq[0]); k++) {
        iq[k].i = (int16_t)(100 + k);
        iq[k].q = (int16_t)(100 - (int)k);
    }
    oran_radio_app_hdr_t app = {
        .data_direction = 1, .payload_version = 1,
        .frame_id = 1, .slot_id = 0, .start_symbol_id = 0,
    };
    oran_uplane_section_t sec = {
        .hdr = { .section_id = 1, .start_prb = 0, .num_prb = 4,
                 .comp_meth = ORAN_COMP_BFP, .iq_bitwidth = 9 },
        .iq = iq, .n_samples = sizeof(iq) / sizeof(iq[0]),
    };
    uint8_t upkt[2048];
    int un = fh_build_uplane(&tx, eaxc, &app, &sec, 1, upkt, sizeof(upkt));

    if (cn < 0 || un < 0) {
        printf("fh demo: build failed\n");
        return EXIT_FAILURE;
    }
    printf("fh demo: built C-plane (%d B) and U-plane (%d B) on eAxC 0x%04x\n",
           cn, un, eaxc);

    /* O-RU: parse the C-plane grant and register it. */
    fh_pkt_t cp;
    if (fh_parse_cplane(&rx, cpkt, (size_t)cn, &cp) != ORU_OK) {
        printf("fh demo: C-plane parse failed\n");
        return EXIT_FAILURE;
    }
    cu_match_add_section1(&m, &cp.cplane.s1);
    printf("fh demo: parsed %s seq=%u -> grant slot=%u PRB[%u..%u)\n",
           fh_pkt_kind_str(cp.kind), cp.ecpri.seq_id, cp.cplane.s1.slot_id,
           cp.cplane.s1.start_prb,
           cp.cplane.s1.start_prb + cp.cplane.s1.num_prb);

    /* O-RU: parse the U-plane and match it against outstanding grants. */
    fh_pkt_t upk;
    oran_uplane_msg_t msg;
    oru_iq16_t out[4 * 12];
    if (fh_parse_uplane(&rx, upkt, (size_t)un, &upk, &msg, out,
                        sizeof(out) / sizeof(out[0])) != ORU_OK) {
        printf("fh demo: U-plane parse failed\n");
        return EXIT_FAILURE;
    }
    oran_uplane_hdr_t uh = {
        .frame_id = msg.app.frame_id, .subframe_id = msg.app.subframe_id,
        .slot_id = msg.app.slot_id, .symbol_id = msg.app.start_symbol_id,
        .start_prb = msg.sec_hdrs[0].start_prb,
        .num_prb = msg.sec_hdrs[0].num_prb,
    };
    cu_match_result_t r = cu_match_check(&m, &uh);
    bool complete = cu_match_grant_complete(&m, uh.frame_id, uh.subframe_id,
                                            uh.slot_id, uh.symbol_id);
    printf("fh demo: parsed %s seq=%u -> match=%s grant_complete=%s\n",
           fh_pkt_kind_str(upk.kind), upk.ecpri.seq_id,
           cu_match_result_str(r), complete ? "yes" : "no");
    printf("fh demo: rx pkts_ok=%llu seq_gaps=%llu\n",
           (unsigned long long)rx.pkts_ok,
           (unsigned long long)rx.seq_gaps);

    return (r == CU_MATCH_OK && complete) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/*
 * PRACH occasion demo (no radio): a DU schedules a PRACH occasion with a
 * C-plane Section Type 3, a UE transmits a Zadoff-Chu preamble at some
 * cyclic shift, and the O-RU detects it by correlation. Shows the
 * Section-Type-3 -> PRACH-detection path end to end.
 */
static int run_prach_demo(void)
{
    const uint16_t root = 22;
    const uint16_t ue_shift = 35;

    /* DU schedules the PRACH occasion (placement carried in Section Type 3). */
    oran_cplane_section3_t s3 = {
        .frame_id = 4, .slot_id = 0, .start_symbol_id = 0,
        .start_prb = 0, .num_prb = 12, .time_offset = 0,
        .frame_structure = 0x31, .cp_length = 144, .freq_offset = 0,
    };
    printf("prach demo: PRACH occasion scheduled (frame=%u slot=%u "
           "PRB[%u..%u) root=%u)\n",
           s3.frame_id, s3.slot_id, s3.start_prb,
           s3.start_prb + s3.num_prb, root);

    /* UE transmits a ZC preamble at ue_shift; add a little noise. */
    oru_iq16_t rx[PRACH_NZC];
    prach_gen_preamble(root, ue_shift, 4000, rx);
    uint32_t st = 0x9e3779b9u;
    for (uint16_t n = 0; n < PRACH_NZC; n++) {
        st = st * 1664525u + 1013904223u;
        rx[n].i = (int16_t)(rx[n].i + (int)((st >> 20) % 401) - 200);
        rx[n].q = (int16_t)(rx[n].q + (int)((st >> 8) % 401) - 200);
    }

    /* O-RU correlates against the known root. */
    prach_detect_t d;
    prach_detect(rx, root, 5.0, &d);
    printf("prach demo: detect %s shift=%u (tx=%u) ratio=%.1f\n",
           d.detected ? "DETECTED" : "MISS", d.shift, ue_shift, d.ratio);

    return (d.detected && d.shift == ue_shift) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/*
 * Beamforming demo (no radio): build a steering beam for a beamId and apply
 * it to a single data stream, printing the per-antenna weights. Shows the
 * C-plane beamId -> per-antenna weight -> IQ path on a 4T array.
 */
static int run_bf_demo(void)
{
    bf_table_t t;
    if (bf_init(&t, 4) != ORU_OK)
        return EXIT_FAILURE;

    const uint16_t beam_id = 1;
    const double theta = 30.0;
    if (bf_set_steering_beam(&t, beam_id, theta) != ORU_OK)
        return EXIT_FAILURE;

    const bf_beam_t *b = bf_get_beam(&t, beam_id);
    printf("bf demo: beam %u steering %.0f deg, %u antennas\n",
           beam_id, theta, b->num_ant);
    for (uint8_t a = 0; a < b->num_ant; a++)
        printf("bf demo:   ant%u weight = (%+d, %+dj) Q1.15\n",
               a, b->w[a].re, b->w[a].im);

    oru_iq16_t in[2] = { {8000, 0}, {0, 8000} };
    oru_iq16_t a0[2], a1[2], a2[2], a3[2];
    oru_iq16_t *out[BF_MAX_ANTENNAS] = { a0, a1, a2, a3 };
    if (bf_apply(&t, beam_id, in, 2, out) != ORU_OK)
        return EXIT_FAILURE;

    printf("bf demo: in[0]=(%d,%d) -> ant0=(%d,%d) ant1=(%d,%d) "
           "ant2=(%d,%d) ant3=(%d,%d)\n",
           in[0].i, in[0].q, a0[0].i, a0[0].q, a1[0].i, a1[0].q,
           a2[0].i, a2[0].q, a3[0].i, a3[0].q);
    return EXIT_SUCCESS;
}

/*
 * CFR demo (no radio): synthesise an OFDM-like waveform, measure its PAPR,
 * apply hard-clip CFR toward a 6 dB target, and show the before/after PAPR
 * and how many samples were clipped.
 */
static int run_cfr_demo(void)
{
    enum { N = 1024 };
    static oru_iq16_t iq[N];
    for (size_t k = 0; k < N; k++) {
        double r = 0.0, im = 0.0;
        for (int t = 1; t <= 8; t++) {
            r  += cos(2.0 * M_PI * t * k / N + t);
            im += sin(2.0 * M_PI * t * k / N + t);
        }
        iq[k].i = (int16_t)(r * 900.0);
        iq[k].q = (int16_t)(im * 900.0);
    }

    cfr_stats_t before, after;
    cfr_measure(iq, N, &before);
    int clipped = cfr_clip(iq, N, 6.0);
    cfr_measure(iq, N, &after);

    printf("cfr demo: PAPR %.2f dB -> %.2f dB (target 6.0), "
           "clipped %d/%d samples\n",
           before.papr_db, after.papr_db, clipped, N);
    return (after.papr_db < before.papr_db) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/*
 * DPD demo (no radio): model a compressive PA, train a pre-distorter by
 * indirect learning, and show the EVM (distortion vs the linear reference)
 * before and after DPD.
 */
static int run_dpd_demo(void)
{
    enum { N = 512 };
    static oru_iq16_t x[N], pd[N], y[N];
    for (size_t k = 0; k < N; k++) {
        double r = 0.0, im = 0.0;
        for (int t = 1; t <= 4; t++) {
            r  += cos(2.0 * M_PI * t * k / N + t);
            im += sin(2.0 * M_PI * t * k / N + t);
        }
        x[k].i = (int16_t)(r * 3000.0);
        x[k].q = (int16_t)(im * 3000.0);
    }

    /* compressive PA model */
    dpd_coeffs_t pa;
    dpd_init(&pa);
    pa.re[0] = 1.0; pa.re[1] = -0.30; pa.re[2] = 0.05; pa.im[1] = 0.05;

    dpd_pa_model(&pa, x, y, N);
    double evm_before = dpd_rms_error_pct(x, y, N);

    dpd_coeffs_t dpd;
    dpd_init(&dpd);
    for (int it = 0; it < 200; it++) {
        dpd_apply(&dpd, x, pd, N);
        dpd_pa_model(&pa, pd, y, N);
        dpd_adapt(&dpd, x, y, N, 0.5);
    }
    dpd_apply(&dpd, x, pd, N);
    dpd_pa_model(&pa, pd, y, N);
    double evm_after = dpd_rms_error_pct(x, y, N);

    printf("dpd demo: PA EVM %.2f%% -> with DPD %.2f%% "
           "(coeffs c1=%.3f c3=%.3f c5=%.3f)\n",
           evm_before, evm_after, dpd.re[0], dpd.re[1], dpd.re[2]);
    return (evm_after < evm_before) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* Print a performance-management interval as YANG/JSON instance data from a
 * small synthetic measurement set (offline; no radio). */
static int run_pm_export(void)
{
    perf_t pm;
    perf_init(&pm);
    for (int i = 0; i < 100; i++) {
        perf_rx_packet(&pm, 1200);
        perf_tx_packet(&pm, 1200);
        perf_dl_window(&pm, 1, 0, 0);
        perf_ul_window(&pm, 1, 0);
        perf_cu_result(&pm, 1, 0, 0);
        perf_evm_sample(&pm, 1.6);
    }
    perf_dl_window(&pm, 0, 0, 1);   /* one late DL */
    perf_seq_gap(&pm);
    perf_snapshot(&pm);

    char json[2048];
    int n = perf_to_json(&pm, json, sizeof(json));
    if (n < 0)
        return EXIT_FAILURE;
    fputs(json, stdout);
    return EXIT_SUCCESS;
}

/* Drive PM->alarm threshold checks over a degraded interval and print the
 * resulting o-ran-fm active-alarm list (offline; no radio). */
static int run_fm_demo(void)
{
    fm_t fm;
    fm_init(&fm);

    fm_thresholds_t th = {
        .dl_late_max = 2, .ul_late_max = 2, .seq_gaps_max = 0,
        .cu_orphan_max = 0, .evm_pct_max = 3.0,
    };

    /* a degraded interval: late DL packets, sequence gaps, high EVM */
    perf_t pm;
    perf_init(&pm);
    for (int i = 0; i < 8; i++)  perf_dl_window(&pm, 0, 0, 1);   /* 8 late */
    for (int i = 0; i < 3; i++)  perf_seq_gap(&pm);
    perf_evm_sample(&pm, 4.5);
    perf_snapshot(&pm);

    size_t active = fm_check_perf(&fm, &pm, &th);
    printf("fm demo: %zu alarm(s) active (max severity %s)\n",
           active, fm_severity_str(fm_max_severity(&fm)));

    char json[1024];
    if (fm_to_json(&fm, json, sizeof(json)) > 0)
        fputs(json, stdout);
    return EXIT_SUCCESS;
}

static int opt_flag(int argc, char **argv, const char *flag)
{
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], flag) == 0)
            return 1;
    return 0;
}

/* Virtual clock for the SIM slot loop: advances one slot per work call. */
typedef struct {
    uint64_t now_ns;
    uint32_t slot_ns;
} sim_clock_t;

static uint64_t sim_clock(void *ctx)
{
    return ((sim_clock_t *)ctx)->now_ns;
}

/* Per-slot work: process one DL packet (on time) and emit one UL packet. */
static oru_status_t sim_slot_work(void *ctx, uint64_t slot,
                                  uint64_t boundary_ns, datapath_t *dp)
{
    sim_clock_t *clk = ctx;

    /* Build a DL U-plane packet and feed it at an on-time arrival. */
    uint8_t dl[2048];
    oru_iq16_t iq[2 * 12];
    for (size_t k = 0; k < sizeof(iq) / sizeof(iq[0]); k++) {
        iq[k].i = (int16_t)(k + slot);
        iq[k].q = (int16_t)-(int16_t)(k + slot);
    }
    oran_uplane_hdr_t h = {
        .num_prb = 2, .comp_meth = ORAN_COMP_BFP, .iq_bitwidth = 9,
    };
    int n = oran_uplane_encode(&h, iq, sizeof(iq) / sizeof(iq[0]),
                               dl, sizeof(dl));
    if (n > 0) {
        uint64_t on_time = boundary_ns - 200000;  /* inside T2a window */
        datapath_handle_dl(dp, dl, (size_t)n, boundary_ns, on_time);
    }

    /* Emit one UL packet on time. */
    uint8_t ul[2048];
    bool late = false;
    datapath_build_ul(dp, 2, ORAN_COMP_BFP, 9, boundary_ns,
                      boundary_ns + 100000, ul, sizeof(ul), &late);

    clk->now_ns += clk->slot_ns;
    return ORU_OK;
}

/*
 * Demonstrate the full slot-cadence datapath: a slot loop drives DL receive
 * (with T2a window enforcement) and UL transmit (Ta3 deadline) for a few
 * slots, then reports the datapath counters. On the target this loop would
 * run on a CPU-pinned thread off the PTP clock.
 */
static void demo_slot_loop(const oru_config_t *cfg, uint32_t scs_hz)
{
    fh_sched_cfg_t scfg = {
        .t2a_min_ns = (uint32_t)oru_config_get_int(cfg, "fronthaul.t2a_min_ns", 100000),
        .t2a_max_ns = (uint32_t)oru_config_get_int(cfg, "fronthaul.t2a_max_ns", 300000),
        .ta3_min_ns = (uint32_t)oru_config_get_int(cfg, "fronthaul.ta3_min_ns", 50000),
        .ta3_max_ns = (uint32_t)oru_config_get_int(cfg, "fronthaul.ta3_max_ns", 200000),
    };

    datapath_t dp;
    if (datapath_init(&dp, &scfg, scs_hz, 0, 0) != ORU_OK)
        return;

    sim_clock_t clk = { .now_ns = 0, .slot_ns = fh_sched_slot_ns(scs_hz) };
    slot_loop_t lp;
    if (slot_loop_init(&lp, &dp, sim_clock, sim_slot_work, &clk, 0,
                       scs_hz) != ORU_OK)
        return;

    slot_loop_run(&lp, 5);
    LOGI(TAG, "datapath demo: DL delivered=%llu dropped=%llu | "
              "UL sent=%llu late=%llu",
         (unsigned long long)dp.stats.dl_delivered,
         (unsigned long long)dp.stats.dl_dropped_late,
         (unsigned long long)dp.stats.ul_sent,
         (unsigned long long)dp.stats.ul_late);

    /* Fold the datapath counters into a PM measurement interval and report
     * it, the way the O-RU would publish o-ran-performance-management. */
    perf_t pm;
    perf_init(&pm);
    for (uint64_t i = 0; i < dp.stats.dl_delivered; i++)
        perf_dl_window(&pm, 1, 0, 0);
    for (uint64_t i = 0; i < dp.stats.dl_dropped_late; i++)
        perf_dl_window(&pm, 0, 0, 1);
    for (uint64_t i = 0; i < dp.stats.ul_sent; i++) {
        perf_tx_packet(&pm, 0);
        perf_ul_window(&pm, dp.stats.ul_late ? 0 : 1, 0);
    }
    for (uint64_t i = 0; i < dp.stats.dl_delivered; i++)
        perf_rx_packet(&pm, 0);
    perf_snapshot(&pm);
    LOGI(TAG, "PM interval %llu: rx_pkts=%llu tx_pkts=%llu dl_late=%llu",
         (unsigned long long)pm.interval_id,
         (unsigned long long)pm.last.rx_packets,
         (unsigned long long)pm.last.tx_packets,
         (unsigned long long)pm.last.dl_late);
}

int main(int argc, char **argv)
{
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    /* Offline tools that need no config/radio. */
    if (opt_flag(argc, argv, "--bench-compression"))
        return run_comp_benchmark();
    if (opt_flag(argc, argv, "--demo-fh"))
        return run_fh_demo();
    if (opt_flag(argc, argv, "--demo-prach"))
        return run_prach_demo();
    if (opt_flag(argc, argv, "--demo-beamform"))
        return run_bf_demo();
    if (opt_flag(argc, argv, "--demo-cfr"))
        return run_cfr_demo();
    if (opt_flag(argc, argv, "--demo-dpd"))
        return run_dpd_demo();
    if (opt_flag(argc, argv, "--export-pm"))
        return run_pm_export();
    if (opt_flag(argc, argv, "--demo-fm"))
        return run_fm_demo();

#ifdef HAL_TARGET
    const char *build = "target";
    int sim = 0;
#else
    const char *build = "host";
    int sim = 1;
#endif

    const char *cfg_path = opt_value(argc, argv, "--config",
                                     "config/oru-config.ini");
    const char *profile  = opt_value(argc, argv, "--profile", NULL);

    LOGI(TAG, "oru_app v%s starting (build=%s, sim=%s)",
         ORU_VERSION_STR, build, sim ? "on" : "off");

    oru_state_t state = ORU_STATE_INIT;
    oru_carrier_cfg_t carrier;
    oru_config_t *cfg = NULL;
    int rc_exit = EXIT_SUCCESS;

    cfg = oru_config_load(cfg_path);
    if (!cfg) {
        LOGE(TAG, "failed to load config %s", cfg_path);
        return EXIT_FAILURE;
    }

    /* --export-yang: validate config and print YANG/JSON instance data,
     * then exit (a handy M-plane sanity check without booting the radio). */
    if (opt_flag(argc, argv, "--export-yang")) {
        oru_carrier_cfg_t c;
        if (oru_config_get_carrier(cfg, &c) != ORU_OK) {
            oru_config_free(cfg);
            return EXIT_FAILURE;
        }
        char err[96];
        if (yang_validate_carrier(&c, err, sizeof(err)) != ORU_OK) {
            LOGE(TAG, "config invalid: %s", err);
            oru_config_free(cfg);
            return EXIT_FAILURE;
        }
        char json[1024];
        int n = yang_carrier_to_json(&c, json, sizeof(json));
        if (n > 0)
            fputs(json, stdout);
        oru_config_free(cfg);
        return (n > 0) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    /* ---------- INIT ---------- */
    LOGI(TAG, "state -> %s", oru_state_str(state));
    if (hal_init(profile) != ORU_OK ||
        splane_init() != ORU_OK ||
        mplane_init() != ORU_OK ||
        fronthaul_init() != ORU_OK) {
        mplane_raise_alarm("INIT_FAIL", "subsystem init failed");
        state = ORU_STATE_FAULT;
        goto done;
    }

    if (hal_jesd204_bringup() != ORU_OK) {
        mplane_raise_alarm("JESD_FAIL", "JESD204 link did not lock");
        state = ORU_STATE_FAULT;
        goto done;
    }

    /* ---------- SYNC ---------- */
    state = ORU_STATE_SYNC;
    LOGI(TAG, "state -> %s", oru_state_str(state));
    if (splane_wait_lock(10000) != ORU_OK || !splane_is_locked()) {
        mplane_raise_alarm("PTP_FAIL", "PTP did not lock");
        state = ORU_STATE_FAULT;
        goto done;
    }

    /* ---------- CONFIG ---------- */
    state = ORU_STATE_CONFIG;
    LOGI(TAG, "state -> %s", oru_state_str(state));
    if (mplane_apply_config(cfg, &carrier) != ORU_OK) {
        mplane_raise_alarm("CFG_FAIL", "could not apply M-plane config");
        state = ORU_STATE_FAULT;
        goto done;
    }
    hal_set_carrier(carrier.center_freq_hz);
    hal_tx_enable(mask_for(carrier.num_tx));
    hal_rx_enable(mask_for(carrier.num_rx));

    /* ---------- OPERATIONAL ---------- */
    state = ORU_STATE_OPERATIONAL;
    LOGI(TAG, "state -> %s", oru_state_str(state));
    LOGI(TAG, "O-RU is on the air: %s %llu Hz, %u MHz, %uT%uR",
         carrier.band, (unsigned long long)carrier.center_freq_hz,
         carrier.bandwidth_hz / 1000000u, carrier.num_tx, carrier.num_rx);

    demo_slot_loop(cfg, carrier.scs_hz);

    /* Main service loop. The real datapath runs in fronthaul RX threads;
     * here we just keep the process alive and watch for loss of sync. */
    while (g_running) {
        if (!splane_is_locked() || !hal_jesd204_is_locked()) {
            mplane_raise_alarm("SYNC_LOST", "lost PTP or JESD lock");
            state = ORU_STATE_FAULT;
            break;
        }
        sleep(1);
#ifndef HAL_TARGET
        /* In SIM, do a single iteration so the demo terminates cleanly. */
        LOGI(TAG, "operational tick (SIM) — exiting demo loop");
        break;
#endif
    }

done:
    LOGI(TAG, "shutting down (final state=%s)", oru_state_str(state));
    fronthaul_shutdown();
    mplane_shutdown();
    splane_shutdown();
    hal_shutdown();
    oru_config_free(cfg);

    if (state == ORU_STATE_FAULT)
        rc_exit = EXIT_FAILURE;
    return rc_exit;
}
