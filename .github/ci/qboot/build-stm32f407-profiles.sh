#!/usr/bin/env bash
set -euo pipefail

profile_list="${1:-.github/ci/qboot/profile-list.txt}"
log_dir="_ci/profile-logs"
profile_jobs="${QBOOT_STM32_PROFILE_JOBS:-2}"
failed_profiles=""
active_jobs=0
active_pids=""

case "$profile_jobs" in
  ''|*[!0-9]*)
    echo "invalid QBOOT_STM32_PROFILE_JOBS: $profile_jobs" >&2
    exit 1
    ;;
esac
if [ "$profile_jobs" -lt 1 ]; then
  echo "QBOOT_STM32_PROFILE_JOBS must be greater than zero" >&2
  exit 1
fi

mkdir -p "$log_dir"
rm -f "$log_dir"/*.status

run_profile() {
  local profile=$1 log_file status_file status
  log_file="$log_dir/$profile.log"
  status_file="$log_dir/$profile.status"

  echo "Build $profile"
  set +e
  bash .github/ci/qboot/build-stm32f407-profile.sh "$profile" > "$log_file" 2>&1
  status=$?
  set -e
  printf '%s\n' "$status" > "$status_file"
  if [ "$status" -ne 0 ]; then
    echo "Profile failed: $profile" >&2
    cat "$log_file" >&2
  fi
}

wait_for_active_profiles() {
  local pid

  for pid in $active_pids; do
    wait "$pid" || true
  done
  active_pids=""
  active_jobs=0
}

while IFS= read -r profile; do
  case "$profile" in
    ''|'#'*) continue ;;
  esac

  run_profile "$profile" &
  active_pids="$active_pids $!"
  active_jobs=$((active_jobs + 1))
  if [ "$active_jobs" -ge "$profile_jobs" ]; then
    wait_for_active_profiles
  fi
done < "$profile_list"

wait_for_active_profiles

while IFS= read -r profile; do
  case "$profile" in
    ''|'#'*) continue ;;
  esac
  if [ ! -f "$log_dir/$profile.status" ] || [ "$(cat "$log_dir/$profile.status")" -ne 0 ]; then
    failed_profiles="$failed_profiles $profile"
  fi
done < "$profile_list"

if [ -n "$failed_profiles" ]; then
  echo "Failed qboot CI profiles:$failed_profiles" >&2
  exit 1
fi

echo "All qboot CI profiles passed. Parallel jobs: $profile_jobs."
