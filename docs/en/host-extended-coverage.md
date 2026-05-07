# QBoot host extended coverage

This file documents the supplemental coverage added by `run-host-extended-coverage.sh` and the related CI templates.

## Coverage

- Protocol-adapter end-to-end length, ordering, overlap, gap, and duplicate-data rejection semantics.
- Production HPatchLite path smoke: the `custom-hpatch-production` runner uses `algorithm/qboot_hpatchlite.c` plus the CI HPatchLite shim for the no-compress single-cover fixture subset, so extended coverage can no longer silently skip the production entry point.
- Fixed-seed RBL parser fuzz corpus plus a structured property manifest for valid boundary packages, non-NUL fields, max version fields, bad magic, header CRC failures, truncation, unsupported algorithms, and size overflow.
- Multi-fault replay convergence smoke cases: one process injects erase/write/sign-read/sign-write failures and verifies custom/FAL backends converge to a jumpable new APP.
- FAL/fake-flash partition offset, cross-boundary, and neighbor-protection current policy.
- Filesystem temp-file, rename, retry, stale-sign, and close-failure atomicity current policy.
- AES+gzip and HPatchLite host-release cross-chunk streaming boundaries.
- HPatchLite resource exhaustion: production shim first malloc failure, success after the first allocation, and host adapter no-hidden-second-allocation current policy.
- Board smoke/HIL template: `run-board-smoke-template.sh` is validated in CI with dry-run mode and can be used on a board runner with `QBOOT_BOARD_FLASH_CMD`, `QBOOT_BOARD_RESET_CMD`, and a log source to check Flash, jump, and reset markers.
- Progress callback, error-code, and host artifact consistency checks.

## Limitation

The production HPatchLite host coverage uses the CI shim for the no-compress single-cover fixture subset. It forces the production `algorithm/qboot_hpatchlite.c` integration entry point, but it is not a full upstream HPatchLite algorithm proof. Full compression combinations, real Flash timing, vector-jump side effects, and reset retention behavior still require board/HIL execution.

By default, missing `qboot_host_runner_custom-hpatch-production` or production fixtures fail extended coverage. Dependency bootstrap jobs may set `QBOOT_HOST_ALLOW_HPATCHLITE_SKIP=1` to record an explicit skip.
