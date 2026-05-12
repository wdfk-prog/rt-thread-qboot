#!/usr/bin/env bash
set -euo pipefail

log_dir="_ci/profile-logs"
summary="$log_dir/rtthread_scons_matrix_summary.md"
matrix_jobs="${QBOOT_STM32_MATRIX_JOBS:-${QBOOT_STM32_PROFILE_JOBS:-2}}"
force_rebuild="${QBOOT_STM32_MATRIX_FORCE_REBUILD:-0}"
case_count=0
active_jobs=0
active_pids=""
pass_count=0
failed_cases=""

case "$matrix_jobs" in
  ''|*[!0-9]*)
    echo "invalid QBOOT_STM32_MATRIX_JOBS: $matrix_jobs" >&2
    exit 1
    ;;
esac
if [ "$matrix_jobs" -lt 1 ]; then
  echo "QBOOT_STM32_MATRIX_JOBS must be greater than zero" >&2
  exit 1
fi
case "$force_rebuild" in
  0|1) ;;
  *)
    echo "invalid QBOOT_STM32_MATRIX_FORCE_REBUILD: $force_rebuild" >&2
    exit 1
    ;;
esac

mkdir -p "$log_dir"
rm -f "$log_dir"/rtthread-scons-*.status \
      "$log_dir"/rtthread-scons-*.log

matrix_cases="
rtthread-scons-qboot-minimal-custom stm32f407-custom-backend
rtthread-scons-qboot-fal stm32f407-fal-backend
rtthread-scons-qboot-fs-dfs stm32f407-fs-backend
rtthread-scons-qboot-custom-algo-basic stm32f407-custom-algo-basic
rtthread-scons-qboot-hpatch stm32f407-hpatch-ram-buffer
rtthread-scons-qboot-all-enabled stm32f407-mixed-app-fal-download-fs
"

if [ "$force_rebuild" -eq 0 ]; then
  while read -r case_name profile; do
    [ -n "$case_name" ] || continue
    profile_status_file="$log_dir/$profile.status"
    if [ ! -f "$profile_status_file" ] || ! grep -Eq '^[0-9]+$' "$profile_status_file"; then
      force_rebuild=1
      break
    fi
  done <<EOF_CASES
$matrix_cases
EOF_CASES
fi

write_matrix_status() {
  local case_name=$1 status=$2
  printf '%s\n' "$status" > "$log_dir/$case_name.status"
}

reuse_profile_status() {
  local case_name=$1
  local profile=$2
  local profile_status_file="$log_dir/$profile.status"
  local status

  if [ ! -f "$profile_status_file" ]; then
    printf 'QBOOT_RTTHREAD_SCONS_FAIL %s missing-profile-status %s\n' "$case_name" "$profile_status_file" >&2
    write_matrix_status "$case_name" 1
    return 1
  fi
  if ! grep -Eq '^[0-9]+$' "$profile_status_file"; then
    printf 'QBOOT_RTTHREAD_SCONS_FAIL %s invalid-profile-status %s\n' "$case_name" "$profile_status_file" >&2
    write_matrix_status "$case_name" 1
    return 1
  fi

  status=$(cat "$profile_status_file")
  write_matrix_status "$case_name" "$status"
  if [ "$status" -eq 0 ]; then
    printf 'QBOOT_RTTHREAD_SCONS_PASS %s reused-profile %s\n' "$case_name" "$profile"
    return 0
  fi

  printf 'QBOOT_RTTHREAD_SCONS_FAIL %s reused-profile %s\n' "$case_name" "$profile" >&2
  if [ -f "$log_dir/$profile.log" ]; then
    cat "$log_dir/$profile.log" >&2
  fi
  return "$status"
}

run_scons_case() {
  local case_name=$1 profile=$2 log_file status=0
  log_file="$log_dir/$case_name.log"

  if [ "$force_rebuild" -ne 1 ]; then
    reuse_profile_status "$case_name" "$profile"
    return $?
  fi

  if bash .github/ci/qboot/build-stm32f407-profile.sh "$profile" > "$log_file" 2>&1; then
    write_matrix_status "$case_name" 0
    printf 'QBOOT_RTTHREAD_SCONS_PASS %s rebuilt-profile %s\n' "$case_name" "$profile"
    return 0
  else
    status=$?
    write_matrix_status "$case_name" "$status"
    printf 'QBOOT_RTTHREAD_SCONS_FAIL %s rebuilt-profile %s\n' "$case_name" "$profile" >&2
    cat "$log_file" >&2
    return "$status"
  fi
}

wait_for_oldest_scons_case() {
  local first_pid rest_pids

  set -- $active_pids
  first_pid=$1
  shift || true
  rest_pids="$*"
  wait "$first_pid" || true
  active_pids="$rest_pids"
  active_jobs=$((active_jobs - 1))
}

start_scons_case() {
  local case_name=$1 profile=$2
  case_count=$((case_count + 1))
  run_scons_case "$case_name" "$profile" &
  active_pids="$active_pids $!"
  active_jobs=$((active_jobs + 1))
  if [ "$active_jobs" -ge "$matrix_jobs" ]; then
    wait_for_oldest_scons_case
  fi
}

while read -r case_name profile; do
  [ -n "$case_name" ] || continue
  start_scons_case "$case_name" "$profile"
done <<EOF_CASES
$matrix_cases
EOF_CASES

while [ "$active_jobs" -gt 0 ]; do
  wait_for_oldest_scons_case
done

while read -r case_name profile; do
  [ -n "$case_name" ] || continue
  status_file="$log_dir/$case_name.status"
  if [ ! -f "$status_file" ]; then
    failed_cases="$failed_cases $case_name"
    continue
  fi
  if ! grep -Eq '^[0-9]+$' "$status_file"; then
    failed_cases="$failed_cases $case_name"
    continue
  fi
  if [ "$(cat "$status_file")" -eq 0 ]; then
    pass_count=$((pass_count + 1))
  else
    failed_cases="$failed_cases $case_name"
  fi
done <<EOF_CASES
$matrix_cases
EOF_CASES

printf '# QBoot RT-Thread SCons Matrix\n\nPassed %d/%d RT-Thread SCons integration cases.\n\n' "$pass_count" "$case_count" > "$summary"
if [ "$force_rebuild" -eq 1 ]; then
  printf 'Mode: force rebuild. Parallel jobs: %s.\n' "$matrix_jobs" >> "$summary"
else
  printf 'Mode: reuse profile build status. Parallel jobs: %s.\n' "$matrix_jobs" >> "$summary"
fi

if [ -n "$failed_cases" ] || [ "$pass_count" -ne "$case_count" ]; then
  printf 'Failed RT-Thread SCons matrix cases:%s\n' "$failed_cases" >&2
  exit 1
fi

if [ "$force_rebuild" -eq 1 ]; then
  printf 'Passed %d/%d QBoot RT-Thread SCons matrix cases. Parallel jobs: %s.\n' "$pass_count" "$case_count" "$matrix_jobs"
else
  printf 'Passed %d/%d QBoot RT-Thread SCons matrix cases by reusing profile build status. Parallel jobs: %s.\n' "$pass_count" "$case_count" "$matrix_jobs"
fi
