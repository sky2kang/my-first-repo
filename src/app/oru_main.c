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
#include "oru/yang.h"

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
}

int main(int argc, char **argv)
{
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

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
