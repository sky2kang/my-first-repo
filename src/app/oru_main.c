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

/*
 * Demonstrate the fronthaul timing-window scheduler. In a real O-RU the
 * RX threads would call fh_sched_classify() for every fronthaul packet
 * against the PTP slot boundary; here we feed it a few synthetic DL
 * arrivals to show on-time / early / late classification and counters.
 */
static void demo_fh_scheduler(const oru_config_t *cfg, uint32_t scs_hz)
{
    fh_sched_cfg_t scfg = {
        .t2a_min_ns = (uint32_t)oru_config_get_int(cfg, "fronthaul.t2a_min_ns", 100000),
        .t2a_max_ns = (uint32_t)oru_config_get_int(cfg, "fronthaul.t2a_max_ns", 300000),
        .ta3_min_ns = (uint32_t)oru_config_get_int(cfg, "fronthaul.ta3_min_ns", 50000),
        .ta3_max_ns = (uint32_t)oru_config_get_int(cfg, "fronthaul.ta3_max_ns", 200000),
    };

    fh_sched_t sched;
    if (fh_sched_init(&sched, &scfg, scs_hz) != ORU_OK)
        return;

    const uint64_t t0 = 0;
    uint64_t boundary = fh_sched_slot_boundary(&sched, t0, 1);
    /* arrivals relative to boundary: too early, on-time, on-time, too late */
    const uint64_t arrivals[] = {
        boundary - scfg.t2a_max_ns - 1,   /* EARLY   */
        boundary - scfg.t2a_max_ns,       /* ON_TIME */
        boundary - scfg.t2a_min_ns,       /* ON_TIME */
        boundary - scfg.t2a_min_ns + 1,   /* LATE    */
    };
    for (size_t i = 0; i < sizeof(arrivals) / sizeof(arrivals[0]); i++) {
        fh_window_result_t r = fh_sched_classify(&sched, FH_DL, boundary,
                                                 arrivals[i]);
        LOGI(TAG, "fh demo: DL arrival[%zu] -> %s", i,
             fh_window_result_str(r));
    }
    LOGI(TAG, "fh demo: DL stats on_time=%llu early=%llu late=%llu",
         (unsigned long long)sched.stats.dl_on_time,
         (unsigned long long)sched.stats.dl_early,
         (unsigned long long)sched.stats.dl_late);
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

    demo_fh_scheduler(cfg, carrier.scs_hz);

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
