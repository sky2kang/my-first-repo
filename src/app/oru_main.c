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
