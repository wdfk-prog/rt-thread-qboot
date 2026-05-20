#!/usr/bin/env bash
set -euo pipefail

while IFS= read -r path; do
  case "$path" in
    ''|'#'*) continue ;;
  esac
  if [ -d "$path" ] || [ -f "$path" ]; then
    continue
  fi
  printf 'missing required package path: %s\n' "$path" >&2
  exit 1
done <<'EOF'
Kconfig
SConscript
inc
src
algorithm
platform
.github/ci/qboot/profile-list.txt
.github/ci/qboot/qboot-host-test-lib.sh
.github/ci/qboot/profiles
.github/ci/qboot/prepare-stm32f407-base.sh
.github/ci/qboot/requirements-stm32.txt
.github/actions/setup-stm32-qboot-ci/action.yml
.github/actions/setup-qboot-host-ci/action.yml
.github/ci/qboot/build-stm32f407-profile.sh
.github/ci/qboot/build-stm32f407-profiles.sh
.github/ci/qboot/check-line-endings.sh
.github/ci/qboot/check-package-files.sh
.github/ci/qboot/check-package-tool-syntax.sh
.github/ci/qboot/check-profile-fragments.sh
.github/ci/qboot/check-kconfig-balance.py
.github/ci/qboot/merge-host-static-checks.sh
.github/ci/qboot/pack-current-policy-artifact.sh
.github/ci/qboot/build-host-sim.sh
.github/ci/qboot/run-host-sim.sh
.github/ci/qboot/run-current-policy-informational.sh
.github/ci/qboot/generate-host-fixtures.py
.github/ci/qboot/run-config-matrix.sh
.github/ci/qboot/run-host-sanitizer.sh
.github/ci/qboot/run-host-static-checks.sh
.github/ci/qboot/prepare-host-codec-packages.sh
.github/ci/qboot/run-host-runtime-codecs.sh
.github/ci/qboot/test-board-smoke-parser.sh
.github/ci/qboot/run-board-smoke-template.sh
.github/ci/qboot/run-host-extended-coverage.sh
.github/ci/qboot/run-rtthread-scons-matrix.sh
tools/package_tool.py
tests/test_package_tool.py
tests/test_package_tool_web.py
tests/package_tool_web_static_checks.py
tests/package_tool_web_test_lib.py
tests/host/qboot_host_runner.c
tests/host/qboot_host_flash.c
tests/host/qboot_host_flash.h
tests/host/qboot_host_crc32.c
tests/host/qboot_host_tinycrypt.c
tests/host/qboot_host_hpatchlite.c
tests/host/stubs/dfs_romfs.h
tests/host/stubs/dfs_file.h
tests/host/stubs/dfs_fs.h
tests/host/stubs/fal.h
tests/host/qboot_host_fal.c
tests/host/stubs/rtthread.h
tests/host/stubs/rtconfig.h
tests/host/stubs/rtdbg.h
tests/host/stubs/crc32.h
tests/host/stubs/tinycrypt.h
docs/package-tool/index.html
docs/package-tool/app.js
docs/package-tool/style.css
docs/package-tool/package_tool_web.py
EOF
