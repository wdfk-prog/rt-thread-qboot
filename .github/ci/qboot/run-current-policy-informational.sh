#!/usr/bin/env bash
set -euo pipefail

out_dir="${QBOOT_HOST_OUT_DIR:-_ci/host-sim}"
current_step="${1:-${QBOOT_CURRENT_POLICY_STEP:-all}}"
python_bin="${PYTHON:-python3}"
export QBOOT_HOST_OUT_DIR="$out_dir"
export QBOOT_CASE_SCOPE=current-policy
export QBOOT_RUN_CURRENT_POLICY_TESTS=1

case "$current_step" in
  all|build-host-sim|host-sim|host-extended|config-matrix|package-tool|package-tool-web) ;;
  *)
    printf 'unsupported current-policy step: %s\n' "$current_step" >&2
    exit 2
    ;;
esac

log_dir="$out_dir/current-policy-logs/$current_step"
summary_dir="$out_dir/current-policy-summaries"
summary="$summary_dir/$current_step.md"
mkdir -p "$log_dir" "$summary_dir"
{
  printf '# QBoot Current-Policy Informational Checks (%s)\n\n' "$current_step"
  printf 'These checks record strategy-dependent behavior. The GitHub Actions job is configured as non-blocking.\n\n'
  printf '| Step | Result | Log |\n|---|---:|---|\n'
} > "$summary"

record_step() {
  local step_name=$1 log_file=$2
  printf 'CURRENT_POLICY_PASS %s\n' "$step_name"
  printf '| %s | PASS | `%s` |\n' "$step_name" "$log_file" >> "$summary"
}

current_policy_log_has_change_signal() {
  local log_file=$1
  grep -Eq \
    'CURRENT_POLICY_CHANGED|QBOOT_(HOST_CASE|HOST_EXTENDED|CONFIG_MATRIX)_FAIL|AssertionError|^[[:space:]]*assert[[:space:]]' \
    "$log_file"
}

record_changed_step() {
  local step_name=$1 log_file=$2 status=$3
  printf 'CURRENT_POLICY_CHANGED %s status=%s\n' "$step_name" "$status"
  printf '| %s | CHANGED | `%s` |\n' "$step_name" "$log_file" >> "$summary"
}

record_failed_step() {
  local step_name=$1 log_file=$2 status=$3
  printf 'CURRENT_POLICY_FAIL %s status=%s\n' "$step_name" "$status"
  printf '| %s | FAIL | `%s` |\n' "$step_name" "$log_file" >> "$summary"
}

run_step() {
  local step_name=$1 log_file=$2 status
  shift 2
  if "$@" > "$log_file" 2>&1; then
    record_step "$step_name" "$log_file"
    return 0
  else
    status=$?
  fi

  if current_policy_log_has_change_signal "$log_file"; then
    record_changed_step "$step_name" "$log_file" "$status"
    cat "$log_file" >&2 || true
    return 1
  fi

  record_failed_step "$step_name" "$log_file" "$status"
  cat "$log_file" >&2 || true
  return 2
}

run_current_policy_step() {
  case "$1" in
    build-host-sim)
      run_step build-host-sim "$log_dir/build-host-sim.log" \
        env QBOOT_HOST_BACKENDS="custom custom-smallbuf fal fs custom-helper custom-quicklz-fastlz custom-hpatch-only fal-hpatch-only fs-hpatch-only mixed-backend custom-product-code-disabled custom-app-check-disabled" \
        bash .github/ci/qboot/build-host-sim.sh
      ;;
    host-sim)
      run_step host-sim "$log_dir/run-host-sim.log" \
        bash .github/ci/qboot/run-host-sim.sh
      ;;
    host-extended)
      run_step host-extended "$log_dir/run-host-extended-coverage.log" \
        bash .github/ci/qboot/run-host-extended-coverage.sh
      ;;
    config-matrix)
      run_step config-matrix "$log_dir/run-config-matrix.log" \
        bash .github/ci/qboot/run-config-matrix.sh
      ;;
    package-tool)
      run_step package-tool "$log_dir/test-package-tool.log" \
        env QBOOT_PACKAGE_TOOL_TEST_OUT="$out_dir/current-policy-package-tool" "$python_bin" -S tests/test_package_tool.py
      ;;
    package-tool-web)
      run_step package-tool-web "$log_dir/test-package-tool-web.log" \
        "$python_bin" -S tests/test_package_tool_web.py
      ;;
    *)
      printf 'unsupported current-policy step: %s\n' "$1" >&2
      return 2
      ;;
  esac
}

changed_count=0
failed_count=0
record_step_result() {
  local status=$1
  case "$status" in
    0) ;;
    1) changed_count=$((changed_count + 1)) ;;
    2) failed_count=$((failed_count + 1)) ;;
    *) failed_count=$((failed_count + 1)) ;;
  esac
}

if [ "$current_step" = "all" ]; then
  for step_name in build-host-sim host-sim host-extended config-matrix package-tool package-tool-web; do
    if run_current_policy_step "$step_name"; then
      record_step_result 0
    else
      record_step_result "$?"
    fi
  done
else
  if run_current_policy_step "$current_step"; then
    record_step_result 0
  else
    record_step_result "$?"
  fi
fi

pass_total=$({ grep -hE 'QBOOT_(HOST_CASE|HOST_EXTENDED|CONFIG_MATRIX)_PASS|package_tool|package_tool_web|CURRENT_POLICY_PASS' "$log_dir"/*.log 2>/dev/null || true; } | wc -l | tr -d ' ')
{
  printf '\nObserved %s current-policy pass markers in `%s`.\n' "$pass_total" "$log_dir"
  if [ "$changed_count" -ne 0 ]; then
    printf 'Changed %d delegated current-policy step(s).\n' "$changed_count"
  fi
  if [ "$failed_count" -ne 0 ]; then
    printf 'Failed %d delegated current-policy step(s) due to harness or infrastructure errors.\n' "$failed_count"
  fi
} | tee -a "$summary"

if [ "$failed_count" -ne 0 ]; then
  printf 'CURRENT_POLICY_FAIL changed=%s failed=%s\n' "$changed_count" "$failed_count"
  exit 1
fi

if [ "$changed_count" -ne 0 ]; then
  printf 'CURRENT_POLICY_CHANGED changed=%s failed=0\n' "$changed_count"
  exit 1
fi

printf 'CURRENT_POLICY_PASS changed=0 failed=0\n'
