# Security Audit: reproducible release build

Base: upstream POOM **1.0.16**, commit
`3533ee027efcf06a610265f3c4b4be8c73548fb5`.
The feature was ported from `l3allzh4ck/the_poom` at source commit
`c703cea793c4e7edf3e3add565735014bdf10316`.
Device Inspector, Wi-Fi Guard and release version 16 remain intact.

Use ESP-IDF **`95b6f32350e1118181d1a1c375972cbcd85a6540`**
(`v6.1-dev-1391-g95b6f32350e`), the published release SDK identified
and used during Wardrive validation. ESP-IDF 6.1 final is different.
Initialize recursive SDK/project submodules and install/export that SDK.
Keep the upstream dependency versions.

## Memory profile

Use upstream `sdkconfig.esp32c5.ram_optimization` with `sdkconfig.defaults`.
No app-specific profile is added or needed. This enables PSRAM malloc,
external BSS, the 512-byte internal threshold and 32 KiB internal reserve,
Wi-Fi/LWIP external allocation and mbedTLS external allocation. It does not
change TLS verification or Wi-Fi IRAM options.

The pinned SDK generates Wi-Fi static RX 16 and RX BA window 16, rather than
the earlier custom profile's 10/6; external mbedTLS allocation also differs.
The new profile and standard four-file SD mount require fresh hardware tests.
The app does not change the shared profile or the shared SD mount defaults.

## Fresh build

Use a fresh build directory with its own generated sdkconfig; defaults do
not reliably override an existing configuration.

```sh
git submodule update --init --recursive
idf.py -B build-security-audit-release -D IDF_TARGET=esp32c5 \
  -D SDKCONFIG=build-security-audit-release/sdkconfig \
  '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.esp32c5.ram_optimization' build size
```

Verify the generated sdkconfig contains every setting in the profile,
especially `SPIRAM_USE_MALLOC=y`,
`SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`, `ALWAYSINTERNAL=512` and
`RESERVE_INTERNAL=32768`. Flash only the intended test device:

```sh
idf.py -B build-security-audit-release -p YOUR_PORT flash
idf.py -B build-security-audit-release -p YOUR_PORT monitor --no-reset
```

## Optional developer checks

Host-only modules, FreeRTOS/SDK stubs and compiler/regression harnesses are
maintained separately on `validation/security-audit-pr51` in the contributor
fork. They are not part of the application PR or firmware build. The
validation branch retains matching source and supplementary check instructions.
The normal application build requires ESP-IDF, not a host GCC/Clang harness.

## Hardware acceptance still required

On an owned test network, verify entry/exit, cancel and hold-B during each
scan/audit phase, reconnect after offline lookup, repeated sessions, missing
and populated SD cards, index rebuild/cancel, report saving and transitions
between Security Audit, Device Inspector, Wi-Fi Guard and BLE apps. Record boot
logs, heap/stack readings and the firmware/configuration hashes. Exercise
Wi-Fi init failure and task-creation failure, and delayed ICMP completion:
ping stop/delete in the pinned SDK is asynchronous, so the caller must wait
for its end callback before releasing callback storage.
