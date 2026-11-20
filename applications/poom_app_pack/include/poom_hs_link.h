// SPDX-License-Identifier: MIT
#pragma once

/*
 * poom_hs_link — a private Wi-Fi station lifecycle for HS VAULT's offload.
 *
 * This is a trimmed, renamed copy of the proven wardrive/WiGLE "transfer data
 * over Wi-Fi" connect path (modules/poom_wardrive/wardrive_wifi.c on
 * feature/wardrive-pr-1.0.16). The stock poom_wifi_ctrl manager's shared STA
 * bring-up (poom_wifi_ctrl_init_sta) no-ops when the driver was already left
 * initialised in another mode by a previous app, so esp_wifi_connect() silently
 * fails to associate (no wifi: driver log, no STA_DISCONNECTED event) — the HS
 * VAULT join bug. This path instead fully owns the station: it brings the
 * driver up from a clean, de-initialised state, so association actually runs.
 *
 * Callers MUST first release the stock driver (poom_wifi_ctrl_deinit()) so the
 * driver is back at ESP_ERR_WIFI_NOT_INIT, then use these entry points, and
 * finally poom_hs_link_deinit() to tear down and restore the prior netif state.
 *
 * Reuses poom_wifi_ctrl's event types so existing callbacks stay compatible.
 */

#include "esp_err.h"
#include "esp_wifi.h"
#include "poom_wifi_ctrl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* True if this module currently owns any Wi-Fi/netif/event-loop resource. */
bool poom_hs_link_owned(void);

/* True once the owned station has an IPv4 lease. */
bool poom_hs_link_sta_has_ip(void);

/* Register / clear the single event callback (CONNECTED / DISCONNECTED / GOT_IP). */
esp_err_t poom_hs_link_register_cb(poom_wifi_ctrl_evt_cb_t cb, void *context);
esp_err_t poom_hs_link_unregister_cb(void);

/* Ask the stock manager to release its own driver if it is sitting idle in
 * WIFI_MODE_NULL (no station/AP, promiscuous off). Returns ESP_ERR_INVALID_STATE
 * if this module already owns the radio or the stock driver is still active. */
esp_err_t poom_hs_link_release_idle_stock(void);

/* Bring up a private STA from a clean (NOT_INIT) driver state.
 * Returns ESP_ERR_INVALID_STATE if the driver is still initialised elsewhere. */
esp_err_t poom_hs_link_init_sta(void);
esp_err_t poom_hs_link_init_sta_with_config(const wifi_init_config_t *config);

/* Set credentials and begin association. Lazily calls init_sta() first. */
esp_err_t poom_hs_link_sta_connect(const char *ssid, const char *password);

/* Disconnect the station (keeps ownership). */
esp_err_t poom_hs_link_sta_disconnect(void);

/* Full teardown: stop + deinit the driver, restore the borrowed netif/default. */
esp_err_t poom_hs_link_deinit(void);

#ifdef __cplusplus
}
#endif
