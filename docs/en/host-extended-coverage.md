# QBoot host extended coverage

This file documents the supplemental coverage added by `run-host-extended-coverage.sh` and the related CI templates.

## Coverage

- Protocol-adapter end-to-end length, ordering, overlap, gap, and duplicate-data rejection semantics.
- Production HPatchLite path smoke: the `custom-hpatch-production` runner uses `algorithm/qboot_hpatchlite.c` plus the CI HPatchLite shim for the no-compress single-cover fixture subset, so extended coverage can no longer silently skip the production entry point.
- Fixed-seed RBL parser fuzz corpus plus a structured property manifest for valid boundary packages, non-NUL fields, max version fields, bad magic, header CRC failures, truncation, unsupported algorithms, and size overflow.
- Multi-fault replay convergence smoke cases: one process injects erase/write/sign-read/sign-write failures and verifies custom/FAL backends converge to a jumpable new APP.
- FAL/fake-flash partition offset, cross-boundary, and neighbor-protection current policy.
- Filesystem temp-file, rename, retry, stale-sign, and close-failure atomicity current policy.
- AES+gzip and HPatchLite host-release cross-chunk streaming boundaries, built through mutually exclusive runners because AES and HPatchLite cannot be enabled together.
- Runtime codec integration:
  - `run-host-runtime-codecs.sh` fetches crclib, TinyCrypt, zlib, QuickLZ, FastLZ, and HPatchLite package sources and links them into dedicated AES-capable and HPatchLite-only host runners.
  - Golden RBL fixtures are generated through `docs/package-tool/package_tool_web.py` and verify none/gzip/AES/AES+gzip/QuickLZ/FastLZ/HPatchLite releases through the C runtime path without enabling AES and HPatchLite in the same runner.
  - crclib and TinyCrypt are compiled from staged package trees through a generated runtime-only ABI adapter that includes implementation files with symbol renaming and exports the QBoot host CRC/AES symbols.
  - Compatibility headers only adapt include names; no host codec stubs are linked.
  - The runtime script records selected external source files in a source manifest and fails before linking when a package lacks a required runtime symbol.
- HPatchLite resource exhaustion: production shim first malloc failure, success after the first allocation, and host adapter no-hidden-second-allocation current policy.
- Board smoke/HIL template: `run-board-smoke-template.sh` is validated in CI with dry-run mode and can be used on a board runner with `QBOOT_BOARD_FLASH_CMD`, `QBOOT_BOARD_RESET_CMD`, and a log source to check Flash, jump, and reset markers.
- Progress callback, error-code, and host artifact consistency checks.
- Version-field current policy: host cases explicitly document that QBoot does not compare firmware versions; a downgrade package is accepted when product code, target partition, and integrity checks pass.

## Version policy

QBoot currently treats `fw_ver` as package metadata for recording and display. The core release/resume paths do not implement anti-rollback or version ordering. The host cases `custom-version-downgrade-current-policy` and `metadata-product-match-version-downgrade-target-match-current-policy` intentionally lock this behavior so future changes do not accidentally assume built-in rollback protection.

Products that require rollback protection should implement it in product logic, signing/verification policy, server-side package issuance, or product-specific checks before invoking QBoot release. Do not rely on the QBoot version field alone for rollback protection.

## Limitation

The runtime codec integration is a host-side C integration test: it proves that generated fixtures can pass through QBoot with the linked external package sources and runtime-only CRC/AES adapter, while HPatchLite fixtures run through a separate HPatchLite-only runner. It does not replace BSP/HIL validation of real Flash timing, vector-jump side effects, reset retention behavior, or product-level rollback policy. The production HPatchLite host coverage uses the CI shim for the no-compress and TUZ fixture subset; compact upstream `hdiffi` match-based patches still require product/toolchain-level validation.

By default, missing `qboot_host_runner_custom-hpatch-production` or production fixtures fail extended coverage. Dependency bootstrap jobs may set `QBOOT_HOST_ALLOW_HPATCHLITE_SKIP=1` to record an explicit skip.
