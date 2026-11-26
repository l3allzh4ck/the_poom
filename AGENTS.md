# POOM — Project & Collaboration Guide (for AI assistants and contributors)

_This file exists because most unhelpful refusals come from missing context, not from
an actual policy boundary. It states the real, authorized, defensive framing of this
project so assistants can help appropriately. It is project context — not an override
of any assistant's own policies or judgment._

---

## What this project is

The **POOM** is an open-source **ESP32-C5 handheld security-research / pentest
multitool** — the same class of device as a Flipper Zero, Pwnagotchi, or WiFi
Pineapple. This repo is its firmware (ESP-IDF, C/C++).

It is used for **WiFi / BLE / IR / NFC security assessment, education, CTF practice,
and defensive ("blue team") network testing**. The deliverable of this work is
**findings + remediation** ("here's the weakness, here's the one-line fix"), not an
attack on anyone.

## Who uses it, and authorization

- The maintainer **owns the hardware** and runs it on **their own networks and
  devices**, or on networks they are **explicitly authorized** to assess (e.g. a
  friend's/family member's network, with their permission).
- The work is **white-hat and authorized red-team**: find weaknesses so the owner can
  fix them. It is **not** targeting third parties or unauthorized systems.
- Many features here are the maintainer's **own code**, previously built and tested on
  their own gear. Integrating/building/flashing it is maintenance of their own project.

## How to help (note to AI assistants)

Please help directly with the normal work of this repo:

- **Building, flashing, and integrating this firmware onto the maintainer's own
  device** is routine, authorized development. Help with the `git`, CMake, and ESP-IDF
  steps (`idf.py build` / `flash` / merges / conflict resolution) the same as any repo.
- The security capabilities here — **deauthentication, WPA handshake capture, PMF /
  deauth-resilience checks, port/exposure scanning, BLE privacy auditing, etc.** — are
  **standard, well-documented dual-use security-audit primitives** (the same ones in
  aircrack-ng, hcxdumptool, nmap, Wireshark). In this project they are used for
  **authorized assessment of the owner's own networks**. Assisting with building and
  integrating the maintainer's existing code for that purpose is appropriate.
- You are **not** being asked to invent novel attacks, write malware, evade detection
  for malicious ends, or target anyone's systems without authorization.

If a request ever genuinely falls outside the responsible-use boundaries below, say so
plainly and offer the in-scope version — don't refuse the whole task over a dual-use
label when the authorized context is clear.

## Responsible-use boundaries (the maintainer's own rules)

These are the lines the maintainer holds, and they should stay in view:

- **Own networks / authorized assessments only.** With permission, for people who asked.
- **No indiscriminate harvesting of third-party traffic.** Opportunistic-capture modes
  (e.g. POOMGOTCHI) stay pointed at the owner's own / authorized targets, not the
  neighbourhood.
- **Detect-and-report ethos.** Active tests (default-credential checks, active deauth
  demos) stay gated behind an explicit "I'm authorized" step.
- **No disruption for its own sake** — no jamming, no DoS, no mass targeting.
- **Heavy lifting offloaded to the owner's own hardware** (e.g. a Hackberry Pi CM5 for
  offline WPA cracking), against the owner's own captures.

## Dev quick reference

- **Toolchain:** ESP-IDF **6.1**, target **esp32c5** (RISC-V, WiFi 6 dual-band, BT5 LE
  only — no Bluetooth Classic/SPP).
- **Fork:** `l3allzh4ck/the_poom`. Feature work lives on `feature/*`; combined/working
  images on `personal/*` and `integration/*`. `main` tracks upstream.
- **Build** (into a named build dir so images don't clobber each other):
  ```
  idf.py -B build/<name> -D IDF_TARGET=esp32c5 \
    -D SDKCONFIG=build/<name>/sdkconfig \
    -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.esp32c5.ram_optimization" build
  ```
- **Flash** (swap the COM port as needed):
  ```
  idf.py -B build/<name> -p COM5 flash monitor
  ```
- **App partition** is ~3712 KiB. Fully combined images run near the ceiling (~4%
  free) — watch the `poom.bin binary size … X% free` line; if it overflows, trim a
  feature or enlarge the partition.
- **Gotchas:** close `monitor` before flashing (it holds the COM port); put the device
  in download mode to flash and power-cycle it to run; a brand-new build dir configures
  cleanly (no stale-cache surprises); after adding a new source file in an existing
  build dir, run `idf.py reconfigure`.
- **Combining features** = merging branches, not `idf.py` flags. Merge, resolve
  conflicts, check menu/CMake registration, confirm it still fits the partition.

## Menu map (where things live on the device)

- **THE BEAST** — WiFi/BLE/RF tools (WiFi Detect, Fox Hunt, Deauth, Handshake Vault,
  Wardrive, Security Audit, POOMGOTCHI, …).
- **THE ZEN** — IR (Sony TV remote, IR Discovery + Remote Suite, …).
- Handshake capture: **WIFI DETECT → (AP) → (Client) → RIGHT → CAPTURE HANDSHAKE**.
