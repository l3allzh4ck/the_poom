# Security Audit PR validation

Prepared against upstream tag `1.0.16` (`3533ee027efcf06a610265f3c4b4be8c73548fb5`).
The original two scanner commits were cherry-picked onto this base in an isolated
worktree. The original source branch is preserved at
`c703cea793c4e7edf3e3add565735014bdf10316`.

## Review changes

- Preserve upstream Device Inspector, Wi-Fi Guard and release version 16.
- Wait for the ICMP end callback after timeout/stop before freeing callback
  storage. The pinned SDK's stop and delete calls do not join the ping task.
- Release Wi-Fi if creating the scanner UI task fails.
- Make host compiler/test/sanitizer failures fail the harness, show the failure
  output and remove isolated temporary files. Add injected-failure regression
  coverage and support invocation outside the repository root.
- Document and supply the opt-in Wardrive-verified release memory profile.
- Correct SD mount comments to describe two file slots (CSV and index).

## Completed checks

- Six existing host suites: audit logic, rules, checks, index, scan logic and
  identification; ASan/UBSan enabled, zero failures.
- GCC 14.2 and Clang 19.1 compile-only checks: C23, warnings as errors.
- Harness regression: injected compiler and executable failures both stop the
  suite, preserve diagnostics and clean temporary files.
- Full ESP32-C5 firmware build and size check with the exact SDK and profile in
  [build instructions](SECURITY_AUDIT_BUILD.md), including the real lwIP backend,
  shared Wi-Fi controller, SD driver and menu integration.
- All 16 explicit release-profile settings verified in generated sdkconfig.
- Upstream defaults, partitions, dependency versions, Device Inspector and
  Wi-Fi Guard sources unchanged. No Wardrive feature code included.
- `git diff --check` clean.

The build has warnings in upstream/third-party code and SDK component dependency
validation; it is not a warning-free build. Host compiler checks for the scanner
are strict. The build's static memory/partition checks do not establish runtime
heap headroom or app-transition behavior.

## Owner hardware checks

On 12 November 2026 the owner verified discovery, full audit, indexed offline
lookup, SD report saving, cancellation/exit and app transitions on ESP32-C5
using the pinned release SDK and memory profile in a separate combined QA
build. After the rename, the owner also confirmed the SECURITY AUDIT menu and
screen titles, successful audit/report saving, renamed report heading and a
responsive main menu on exit.

These are owner-reported hardware results; the cloud environment has no device.
The hardware build also contained the separate handshake and Wardrive features,
and enabled Wardrive diagnostics. The standalone branch was built separately
but was not separately flashed. Hardware checks with diagnostics disabled and
ESP32-C6 validation are not claimed.

## Integration note

The combined QA exercise exposed a DNS-restoration issue in the separate
Wardrive implementation after borrowing a station interface. Its fix and the
Wardrive navigation improvements remain on the QA branch, outside this PR.
The tested combined Wi-Fi/BLE transition used that fix. When combining with
Wardrive PR #50, apply its focused DNS cleanup fix as well; Security Audit
itself does not depend on the Wardrive feature.

## Maintainer scope revision (hardware check pending)

- Reuse upstream `sdkconfig.esp32c5.ram_optimization`; delete the duplicate
  app-specific memory profile. Do not modify the shared profile.
- Keep host test modules, FreeRTOS/SDK stubs and harness scripts separately on
  `validation/security-audit-pr51`, outside this application PR.
- Restore the shared Wi-Fi controller to upstream. A failed startup unwinds
  an app-created driver locally; a previously initialized driver is not
  deinitialized by that failure path.
- Use standard `sd_card_mount()` settings (four slots), removing the lean-mount
  API and the separate unmount bookkeeping change from this PR.
- Retain only an additive, read-only raw mount-error accessor in the SD driver.
  Upstream maps multiple card-init causes (including allocation failure) to
  the same public error. The accessor lets the app distinguish a missing/bad
  card from memory pressure that may recover after radio teardown. It does
  not change mount defaults, filesystem behavior or other callers.

These configuration and mount-policy changes invalidate earlier standalone/
combined hardware acceptance for the revised configuration. Repeat SD present/
absent, index generation, report saving, cancellation and Wi-Fi/BLE transitions
on owned test devices before merge. No exploit execution or password attempts
are part of these changes or their validation.

Maintainer-review build check: full ESP32-C5 build on the pinned release SDK
with the unmodified upstream RAM profile passed. Image 0x37b0a0 retains 4% app-slot headroom.
Six optional host suites and GCC strict compile checks passed; the optional
Clang compile pass was unavailable because Clang is not installed in the
validation environment. The harness failure-propagation test passed. An
additional production-startup-block test confirmed app-created failed drivers
are unwound and pre-existing drivers are left untouched.
Hardware acceptance of this revised profile remains pending.
