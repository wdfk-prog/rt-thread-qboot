#!/usr/bin/env bash
set -euo pipefail
log_dir="_ci/profile-logs"
summary="$log_dir/rtthread_scons_matrix_summary.md"
mkdir -p "$log_dir"
pass_count=0
case_count=0
run_scons_case() {
  case_name=$1; profile=$2; log_file="$log_dir/$case_name.log"
  case_count=$((case_count + 1))
  if bash .github/ci/qboot/build-stm32f407-profile.sh "$profile" > "$log_file" 2>&1; then
    pass_count=$((pass_count + 1)); printf 'QBOOT_RTTHREAD_SCONS_PASS %s\n' "$case_name"
  else
    printf 'QBOOT_RTTHREAD_SCONS_FAIL %s\n' "$case_name"; cat "$log_file"; exit 1
  fi
}
run_scons_case rtthread-scons-qboot-minimal-custom stm32f407-custom-backend
run_scons_case rtthread-scons-qboot-fal stm32f407-fal-backend
run_scons_case rtthread-scons-qboot-fs-dfs stm32f407-fs-backend
run_scons_case rtthread-scons-qboot-custom-algo-basic stm32f407-custom-algo-basic
run_scons_case rtthread-scons-qboot-hpatch stm32f407-hpatch-ram-buffer
run_scons_case rtthread-scons-qboot-all-enabled stm32f407-mixed-app-fal-download-fs
printf '# QBoot RT-Thread SCons Matrix\n\nPassed %d/%d RT-Thread SCons integration cases.\n' "$pass_count" "$case_count" > "$summary"
printf 'Passed %d/%d QBoot RT-Thread SCons matrix cases.\n' "$pass_count" "$case_count"
