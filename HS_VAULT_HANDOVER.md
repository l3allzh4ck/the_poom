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

## ⛔ OPEN BUG — the WiFi join (this is the next job)
HS VAULT's `sta_connect` returns `ESP_OK` but the STA **never associates or gets an IP**, on both
2.4GHz and 5GHz, with **no `wifi:` driver logs and no `STA disconnected` event** — so the driver
isn't even attempting association. Serial trace (the app logs `[HV] ...` lines):
```
[HV] start: 3 profile(s)
[HV] join 1/3 ssid='Telstra58853F' pass=yes
[HV] sta_connect rc=ESP_OK
[HV] join timeout (attempt 1/3)   (x3, then same on the -5G SSID)
```
**Planned fix (user's idea, good one):** reuse the **wardrive / WiGLE "transfer data over WiFi"
path**, which joins WiFi and ships data successfully. Lift its connect+transfer approach into
HS VAULT's send instead of the current `poom_wifi_ctrl_sta_connect` + `esp_http_client` path.
Look at `feature/wardrive-pr-1.0.16` (and `fix/wardrive-idle-radio-handoff`) for that code.
Candidate alternative fixes if needed: full radio teardown (`poom_wifi_ctrl_deinit()`→`init_sta()`)
before connect; check ESP32-C5 `esp_wifi_set_band_mode`; bump `esp_log_level_set("wifi", VERBOSE)`
to see the real driver reason. The user's F: clone also has post-handoff diagnostics commits
(`cbdf4ec`, branches `diagnostics/connection-test-20261020`, `diagnostics/network-dhcp-20261020`,
`fix/hs-vault-sony-diagnostics-20261019`) that may already contain a fix — ask the user to push
them if useful.

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
