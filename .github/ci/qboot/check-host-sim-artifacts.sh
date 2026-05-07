#!/usr/bin/env bash
set -euo pipefail

out_dir="_ci/host-sim"
summary="$out_dir/artifact_check_summary.md"
main_log=${1:-}
status=0

require_file() {
  if [ ! -s "$1" ]; then
    printf 'QBOOT_HOST_ARTIFACT_FAIL missing-or-empty %s\n' "$1" >&2
    status=1
  fi
}

require_file "$out_dir/qboot_host_sim_summary.md"
require_file "$out_dir/qboot_c_header.json"
require_file "$out_dir/qboot_py_header_expected.json"
require_file "$out_dir/config-matrix/config_matrix_summary.md"
require_file "$out_dir/sanitizer/sanitizer_summary.md"
require_file "$out_dir/static-checks/static_checks_summary.md"
require_file "$out_dir/extended_coverage_summary.md"

if [ -n "$main_log" ] && [ -f "$main_log" ]; then
  pass_lines=$(grep -c '^QBOOT_HOST_CASE_PASS ' "$main_log" || true)
  unique_lines=$(grep '^QBOOT_HOST_CASE_PASS ' "$main_log" | awk '{print $2}' | sort -u | wc -l | tr -d ' ')
  if [ "$pass_lines" != "$unique_lines" ]; then
    printf 'QBOOT_HOST_ARTIFACT_FAIL duplicate-case-pass-lines total=%s unique=%s\n' "$pass_lines" "$unique_lines" >&2
    status=1
  fi
fi

if [ "$status" -ne 0 ]; then
  exit "$status"
fi
printf '# QBoot Host Artifact Check\n\nArtifact presence and pass-line uniqueness checks passed.\n' > "$summary"
printf 'QBOOT_HOST_ARTIFACT_PASS checked artifacts\n'
