// SPDX-License-Identifier: MIT
// Copyright (c) 2026 THE POOM

#ifndef MENU_HS_VAULT_H
#define MENU_HS_VAULT_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Launches HS Vault (THE BEAST > HS VAULT).
 *
 * Browses captured WPA handshakes on the SD card, grouped/sorted by AP, and
 * pushes a selected capture to a cracking rig (the Hackberry Pi) over WiFi.
 *
 * Reads the hashcat `.22000` sidecars that `poom_pcap` writes:
 *   - /sdcard/pcaps/handshakes/        (auto / POOMGOTCHI + PCAP sniffer) -> tag [A]
 *   - /sdcard/pcaps/handshakes/manual/ (deliberate captures)             -> tag [M]
 *
 * Each `.22000` line already carries the AP BSSID and ESSID, so the list is
 * built and sorted by SSID without touching the raw pcap.
 *
 * Flow: LIST (sorted by AP) -> A: DETAIL (AP/BSSID/type/size) -> A: SEND.
 * Sending joins a saved WiFi profile (shared poom_wifi_profile_store, same set
 * as Settings > WIFI PROFILES), waits for an IP, then HTTP POSTs the `.22000`
 * to the configured endpoint as application/octet-stream. The endpoint URL is
 * editable on-device (RIGHT in DETAIL) and persisted in NVS.
 *
 * This moves a capture the operator already made to the operator's own rig; it
 * performs no RF transmission, so it needs no active-test authorization gate.
 * Own gear / consented CTF only.
 *
 * @return void
 */
void app_hs_vault(void);

#ifdef __cplusplus
}
#endif

#endif /* MENU_HS_VAULT_H */
