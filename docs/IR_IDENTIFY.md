# IR IDENTIFY

IR IDENTIFY looks for a Mute signal that works with a TV you own or have permission to test. A Mute signal can be shared by many remotes. The results list shows every associated profile in the installed database, and informs you when you confirm a response to another command.

## Prepare the SD card

Use Python 3.10 or later and the [Flipper-IRDB](https://github.com/Lucaslhm/Flipper-IRDB) checkout. The firmware uses its `TVs` folder and the [Flipper infrared remote format](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/documentation/file_formats/InfraredFileFormats.md). The database is generated locally. The source project dedicates its database under CC0-1.0. The generator copies the upstream license and records the source URL and revision beside the prepared files. Preserve those files when distributing the prepared directory.

From the POOM checkout:

```sh
git clone --depth 1 https://github.com/Lucaslhm/Flipper-IRDB.git
python3 tools/prepare_ir_identify.py Flipper-IRDB /path/to/sd-card/ir/identify
```

The destination must be new. When updating, prepare a new directory off-card and replace the entire `ir/identify` directory together. The index and numbered profiles belong to the same build. The expected layout is:

```text
ir/
  identify/
    tv.idx
    profiles/
      00000000.ir
      ...
    LICENSE-Flipper-IRDB.txt
    SOURCE.txt
  remotes/
    ... saved remotes
```

The checkout at `d126fb1b6f1e114c52b4a8c19839ea65e3a9c24d` produced 93 unique Mute signals, 357 profiles and 358 associations. The generator skipped 17 unsupported source commands across the TV collection.

## Find and save a remote

1. Open **THE ZEN → IR IDENTIFY**. Aim the POOM IR emitter at the TV and press **A**.
2. Press **A** during a group as soon as you see or hear a Mute response. If you watch the full group, answer **YES** if the TV responded at any time, even if a later command restored the original sound. Select **REPEAT** if uncertain. **B** stops the scan after any press already in flight.
3. For the final candidates, press **A** to send each individual Mute command and select **YES** only when the TV responds to that press. If a positive group cannot be reproduced, the scan returns to its untested candidates. When the scan runs out, choose **SLOW RETRY** for an 800 ms gap between commands or **RESTART** for the default 350 ms gap.
4. Scroll through compatible profiles with **Up/Down** and open one with **A**. **Left/Right** moves a long profile label. Test the suggested command, or use **Up/Down** to choose another. The suggestion prefers a Volume command whose signal differs across profiles when available. Sending each command requires **A**.
5. Confirm a response to an additional command, then select **SAVE TO SD**. If the verified Mute is the only verification option, the profile can be saved using that confirmation. Enter a name on the on-screen keyboard. Existing names are protected. The file appears under `/ir/remotes/<name>.ir` and can be opened from **THE ZEN → IR UNIV**.

## Protocols and limits

- Parsed playback uses the POOM RMT driver for NEC, NECext, NEC42, NEC42ext, Samsung32, RC5, RC5X, RC6, SIRC, SIRC15, SIRC20, RCA, Pioneer and Kaseikyo. NECext in prepared files preserves both command bytes. SIRC uses three frames per press. Parsed RC5/RC5X and RC6 alternate their toggle bit on each press within a session.
- RAW playback preserves supplied timings and frequency within POOM's limits of 10–60 kHz, 7–1,024 intervals and 3 ms–2 s. It removes trailing idle intervals and rounds carrier duty cycle to thousandths. A fixed RAW capture can contain repeated frames or a fixed toggle bit.
- Deduplication uses encoded parsed payloads, including equivalent NEC and NECext frames, and exact normalized RAW data. Similar RAW captures with different measured timings remain separate candidates. RAW and parsed records may represent the same physical button.
- The SD database supports up to 4,096 indexed Mute signals and 255 supported commands per profile. Discovery reads only the relevant offsets and reuses one scratch buffer. A verified profile remains compatible for the commands you actually tested.
- Legacy IR UNIV parsed remotes preserve their existing playback conventions. IR IDENTIFY exports use a header comment to select full NECext playback when opened in IR UNIV.

## Build and flash

Use the repository's [ESP32-C5 build instructions](BUILD_C5.md) and ESP-IDF v6.1 at `fff9895c82d744c7237be8847347bdd1b07c6643`. With the exported SDK environment active, this command uses the two defaults tracked at the feature branch's base revision:

```sh
idf.py -B build/esp32c5 -D IDF_TARGET=esp32c5 \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.esp32c5.ram_optimization' build size
idf.py -B build/esp32c5 -p /dev/ttyACM0 flash monitor
```

Use an empty build directory when changing defaults. For a binary package, use the included `flash_args` from inside the package with `esptool --chip esp32c5 --port /dev/ttyACM0 write-flash @flash_args`. For an existing installation whose bootloader and partitions should be retained, use the project's normal app-only update flow with `poom.bin`.

## Hardware checklist

- Boot the ESP32-C5 POOM with its SD card inserted. Confirm startup, back navigation and clean relaunch. Repeat with no SD card, a missing index, a truncated index, an unsupported protocol and a missing profile.
- Test known parsed and RAW-TV profiles. Watch for transient Mute changes, use **A** partway through a group, and confirm that no further unstarted press is emitted. Repeat with **B** during a frame and during the inter-command gap, then verify IR is idle after returning to the menu.
- Test all-NO, REPEAT, slow retry, one and two candidates, uneven groups, and a deliberate false group hit that falls back to the other untested ranges.
- With a logic analyser or IR receiver, check carrier, RAW timing, SIRC spacing and RC5/RC6 toggle bits. Check that another POOM TX caller receives a busy error while IR IDENTIFY owns the pin and can send after it exits.
- Open a shared-Mute result. Confirm additional commands on one profile, save it under a new name, and replay both parsed and RAW commands through IR UNIV. Check duplicate-name handling, card removal during save, and that a previous remote remains readable.
- Smoke-test TV OFF, legacy IR UNIV learned and SD commands, and POOM Web IR after leaving IR IDENTIFY.
