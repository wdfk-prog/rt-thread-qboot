#!/usr/bin/env bash
set -euo pipefail

out_dir="${QBOOT_HOST_OUT_DIR:-_ci/host-sim}"
step_name="${1:-${QBOOT_CURRENT_POLICY_STEP:-all}}"
archive="_ci/qboot-current-policy-informational-${step_name}.tar"
summary="$out_dir/current-policy-summaries/$step_name.md"
log_dir="$out_dir/current-policy-logs/$step_name"

mkdir -p "$log_dir" "$(dirname "$summary")" _ci
if [ ! -f "$summary" ]; then
  printf '# QBoot Current-Policy Informational Checks (%s)\n\nRunner did not produce a summary before exiting.\n' \
    "$step_name" > "$summary"
fi

tar -cf "$archive" \
  -C . \
  "$log_dir" \
  "$summary"
