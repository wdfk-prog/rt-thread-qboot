#!/usr/bin/env bash
set -euo pipefail

profile_list="${1:-.github/ci/qboot/profile-list.txt}"
log_dir="_ci/profile-logs"
failed_profiles=""

mkdir -p "$log_dir"

while IFS= read -r profile; do
  case "$profile" in
    ''|'#'*) continue ;;
  esac

  log_file="$log_dir/$profile.log"
  echo "::group::Build $profile"
  set +e
  bash .github/ci/qboot/build-stm32f407-profile.sh "$profile" 2>&1 | tee "$log_file"
  status=${PIPESTATUS[0]}
  set -e
  echo "::endgroup::"

  if [ "$status" -ne 0 ]; then
    failed_profiles="$failed_profiles $profile"
    echo "Profile failed: $profile" >&2
  fi
done < "$profile_list"

if [ -n "$failed_profiles" ]; then
  echo "Failed qboot CI profiles:$failed_profiles" >&2
  exit 1
fi

echo "All qboot CI profiles passed."
