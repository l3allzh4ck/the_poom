# HS VAULT — handover (2026-11-20)

_Pick-up notes for a fresh chat. Remove this file before the PR merges if you like._

## Where things are
- **Branch: `feature/hs-vault`** on fork `l3allzh4ck/the_poom`, based on `community-pack-v1`.
  Pushed. This is the working branch — do everything from GitHub, no local clone needed.
- HS VAULT was **reconstructed from an earlier chat's source** (the original branches
  `feature/poom-netdef` / `integration/poom-1.0.13` were deleted from the fork during the
  community-pack reorg; the only other copy is on the user's F: machine).

## What HS VAULT is (and is NOT)
- **HS VAULT (ours)** = `applications/poom_app_pack/src/menu_hs_vault.c` (+ header), wired in
  `applications/poom_menu/src/poom_menu.c` as `THE BEAST > HS VAULT`. It **browses** captured
  WPA `.22000` handshakes on the SD card (sorted by AP; BSSID/SSID/type parsed straight from the
  hashcat line), and on A→A **sends** the selected `.22000` over WiFi to a cracking rig via
  `POST http://<ip>:<port>/upload?name=<file>` (octet-stream). Endpoint URL editable on-device
  (RIGHT in DETAIL), persisted in NVS (`poom_secrets_store`, key `hs_pi_url`). Joins a saved WiFi
  profile (`poom_wifi_profile_store`) to reach the rig. File transfer only, no RF TX → no auth gate.
- **Pi side** = `tools/hackberry_receiver/poom_handshake_receiver.py` (+README): stdlib HTTP
  listener on :8080, saves upload, auto-runs `hashcat -m 22000`.
- **NOT the same as** the on-device cracker on `feature/wpa-handshake`
  (`menu_handshake_crack.c`) — that's a separate, later feature that cracks on the POOM itself.

## ✅ WiFi join bug — FIX IMPLEMENTED (needs flash verification on F:/COM13)
**Symptom was:** HS VAULT's `sta_connect` returned `ESP_OK` but the STA **never associated or got
an IP**, on both 2.4GHz and 5GHz, with **no `wifi:` driver logs and no `STA disconnected` event**:
```
[HV] start: 3 profile(s)
[HV] join 1/3 ssid='Telstra58853F' pass=yes
[HV] sta_connect rc=ESP_OK
[HV] join timeout (attempt 1/3)   (x3, then same on the -5G SSID)
```
**Root cause:** HS VAULT used the shared `poom_wifi_ctrl` manager. `poom_wifi_ctrl_init_sta()`
**no-ops when the driver was already initialised in another mode** by a previous app (a capture
app in monitor mode, the stock manager in AP/NULL, etc.). `esp_wifi_connect()` is then a call into
a driver that is not in a clean STA state → it returns OK but silently does nothing: no association,
no disconnect event. Exactly the trace above.

**Fix (as planned — the wardrive/WiGLE "transfer over WiFi" path):** added a private, fully-owned
station lifecycle vendored from `feature/wardrive-pr-1.0.16`'s proven `wardrive_wifi.c`:
- **New files:** `applications/poom_app_pack/src/poom_hs_link.c` + `include/poom_hs_link.h`
  (trimmed/renamed copy; BT-exclusivity guard dropped since HS VAULT drives no BLE). It brings the
  driver up **from a clean `ESP_ERR_WIFI_NOT_INIT` state**, owns its own event loop / netif
  (borrowing & restoring the stock `WIFI_STA_DEF` netif) / WIFI+IP event handlers, and fully
  de-inits on teardown.
- **`menu_hs_vault.c` rewired:** no longer brings up the stock STA on entry. At send time it forces
  the stock manager fully off (`set_promiscuous(false)` → `sta_connect`-disconnect →
  `poom_wifi_ctrl_deinit()`) **then** joins via `poom_hs_link_sta_connect()`. IP wait / disconnect /
  teardown all go through `poom_hs_link_*`. The `esp_http_client` POST is unchanged.
- **`CMakeLists.txt`:** added `esp_event esp_netif esp_wifi` to REQUIRES (the new file calls them
  directly). The glob picks up the new `.c` — **run `idf.py reconfigure` before building.**

**Status: compiles against the base by inspection, NOT yet toolchain-verified / flashed.** Next
step is to build on F: (COM13) and confirm the STA associates + gets an IP and the `.22000` POST
lands on the Pi. If it still fails, bump `esp_log_level_set("wifi", ESP_LOG_VERBOSE)` to read the
real driver reason, and check ESP32-C5 `esp_wifi_set_band_mode`. The user's F: clone also has
post-handoff diagnostics commits (`cbdf4ec`, branches `diagnostics/connection-test-20261020`,
`diagnostics/network-dhcp-20261020`, `fix/hs-vault-sony-diagnostics-20261019`) worth comparing.

## Build / test
- New source file → **`idf.py reconfigure`** before `idf.py build` (CMake globs are cached; this bit
  us before — a plain build silently omits the file, `idf.py fullclean` fixes it).
- No ESP-IDF toolchain in the cloud session — the user builds/flashes on **F: (`F:\Projects\POOM\
  poom-netdef`), COM13**. All external symbols were verified present on the base, so it should
  compile, but it is **not** toolchain-verified.
- Pi: `rockyou.txt` often ships gzipped on Kali → `sudo gunzip /usr/share/wordlists/rockyou.txt.gz`;
  on an ARM SBC hashcat may need `pocl-opencl-icd`.

## PR plan
User wants a PR. Recommendation: **fix the join bug first, then open a clean PR** targeting
`community-pack-v1` (the active line it's built on; `main` is still 1.0.13 and lacks the profile
store). A draft PR now is fine too if the user prefers to track it.

## ⚠️ Ethics (unchanged, important)
HS VAULT lists captures including `<unknown>` ones that are likely **POOMGOTCHI roaming grabs of
third-party networks**. Only crack **own / consented** networks. Open TODO: add an "is this BSSID
one of mine?" allowlist guard before send. Keep this framing in any PR description.

## Repo landscape notes
- `community-pack-v1` = active integration line; has the profile store (`poom_wifi_profile_store`)
  and our keyboard cursor + `:` key edits (those survived). Connection Test app itself was dropped
  but its backing store stayed.
- `origin/main` = 1.0.13 (`2b12efd`), no profile store.
- Lots of PR/review/validation branches (#47–#51, wardrive, net-scanner-auditor, deep-sleep).
