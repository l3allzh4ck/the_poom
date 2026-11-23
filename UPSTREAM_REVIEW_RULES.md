# Upstream (The‑POOM) Contribution Rules

Recurring review feedback from the upstream maintainer (**jonathancagua**), distilled
from PRs #49 / #50 / #51. Satisfy every point here **before** opening any PR to
`The-POOM/the_poom`.

> ⚠️ **This file must never appear in an upstream PR diff.** It lives on the
> `notes/upstream-review-rules` branch of our fork only. Keep it off every
> feature branch that a PR is cut from.

---

## Hard rules

1. **Use current ESP‑IDF v6.1 APIs.** No deprecated/legacy forms.
   - Specific case he flagged: GPIO deep‑sleep wake must use
     `esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(...)`, not the older
     GPIO‑wakeup call.

2. **No new sdkconfig profiles.** Reuse the existing
   `sdkconfig.esp32c5.ram_optimization`. Don't add or edit `sdkconfig*` files
   unless genuinely unavoidable — and justify it explicitly if so.

3. **Keep shared‑component changes minimal, and justify each one.**
   - **Do not touch `modules/poom_wifi_ctrl`** unless there is no alternative.
     Prefer isolating radio/band tweaks inside your own feature's path (e.g. the
     capture's APSTA path) rather than the shared init.
   - For any shared module you must change (`poom_pcap`, `poom_wifi_attacks`, …),
     state in the PR *why each change is strictly necessary*.

4. **No FreeRTOS stubs or host‑test tooling** in the PR.

5. **No regressions in existing apps.** Confirm existing apps still build and run
   after your change.

6. **No AI co‑author attribution** on commits or PR descriptions.

---

## Pre‑PR checklist (run against the merge base, e.g. `upstream/main`)

```bash
BASE=upstream/main   # or the exact upstream commit you branched from

# 2. sdkconfig untouched -> expect empty
git diff --stat "$BASE"..HEAD -- '*sdkconfig*' 'sdkconfig*'

# 3. poom_wifi_ctrl untouched -> expect empty
git diff --stat "$BASE"..HEAD -- 'modules/poom_wifi_ctrl/*'

# 4. no test/stub/host tooling -> expect empty
git diff --name-only "$BASE"..HEAD | grep -iE 'test|stub|mock|host'

# 1. no stray/deprecated sleep or wakeup APIs -> inspect any hits
git diff "$BASE"..HEAD | grep -iE '^\+.*(esp_sleep|gpio_wakeup|wakeup_on_hp)'

# shared-module footprint -> list it, justify each in the PR body
git diff --stat "$BASE"..HEAD -- 'modules/*'
```

An empty result for the first three and a justified, minimal list for the last
means the diff is in line with his feedback.

---

## PR description conventions

- Match the tone and structure of the previously accepted PRs (#49 / #50 / #51).
- Note build‑on relationships (e.g. "builds on #49").
- Include a short, explicit list of which **shared‑component** changes were
  required and why.
- No AI attribution (see rule 6).

---

## Project constraints worth remembering

- **Internal SRAM is the scarce resource** on the ESP32‑C5, not flash. Heavy
  Wi‑Fi apps compete for a large *contiguous* internal block; once fragmented,
  the hungriest consumers (stock‑config Wi‑Fi, Spectrum, Device Inspector) can
  fail to allocate even when total free looks fine. This is a device‑wide
  ceiling — raise it as its own issue; don't try to "fix" it inside a feature's
  teardown. Lean Wi‑Fi buffer configs (as in the wardrive transfer path) are the
  accepted mitigation for a feature's own footprint.
