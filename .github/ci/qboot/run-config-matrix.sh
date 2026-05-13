#!/usr/bin/env bash
set -euo pipefail

host_root="${QBOOT_HOST_OUT_DIR:-_ci/host-sim}"
out_dir="$host_root/config-matrix"
fixture_dir="$host_root/fixtures"
log_dir="$out_dir/logs"
summary="$out_dir/config_matrix_summary.md"
results="$out_dir/config_matrix_results.csv"
mkdir -p "$log_dir"
printf 'case,kind,result,command,log\n' > "$results"
pass_count=0
case_count=0

record() {
  local case_name=$1 kind=$2 result=$3 command_name=$4 log_file=$5
  case_count=$((case_count + 1))
  [ "$result" = PASS ] && pass_count=$((pass_count + 1))
  printf '%s,%s,%s,%s,%s\n' "$case_name" "$kind" "$result" "$command_name" "$log_file" >> "$results"
}

run_valid_build() {
  local case_name=$1 backends=$2 extra_cflags=${3:-}
  local log_file="$log_dir/$case_name.log"
  if QBOOT_HOST_OUT_DIR="$host_root" QBOOT_HOST_BACKENDS="$backends" QBOOT_HOST_EXTRA_CFLAGS="$extra_cflags" bash .github/ci/qboot/build-host-sim.sh > "$log_file" 2>&1; then
    printf 'QBOOT_CONFIG_MATRIX_PASS %s\n' "$case_name"; record "$case_name" valid PASS build "$log_file"
  else
    printf 'QBOOT_CONFIG_MATRIX_FAIL %s\n' "$case_name"; cat "$log_file"; record "$case_name" valid FAIL build "$log_file"; exit 1
  fi
}

run_expected_build_fail() {
  local case_name=$1 backends=$2 expected_pattern=$3
  local log_file="$log_dir/$case_name.log"
  if QBOOT_HOST_OUT_DIR="$host_root" QBOOT_HOST_BACKENDS="$backends" bash .github/ci/qboot/build-host-sim.sh > "$log_file" 2>&1; then
    printf 'QBOOT_CONFIG_MATRIX_FAIL %s unexpected-pass\n' "$case_name"; record "$case_name" invalid FAIL build "$log_file"; exit 1
  fi
  if grep -Eq "$expected_pattern" "$log_file"; then
    printf 'QBOOT_CONFIG_MATRIX_PASS %s\n' "$case_name"; record "$case_name" invalid PASS build "$log_file"
  else
    printf 'QBOOT_CONFIG_MATRIX_FAIL %s unexpected-failure\n' "$case_name"; cat "$log_file"; record "$case_name" invalid FAIL build "$log_file"; exit 1
  fi
}

run_expected_runtime_reject() {
  local case_name=$1 runner=$2 package=$3 new_app=$4
  local log_file="$log_dir/$case_name.log"
  if "$runner" --case "$case_name" --package "$package" --old-app "$fixture_dir/old_app.bin" --new-app "$new_app" --expect-receive 1 --expect-first-success 0 --expect-success 0 --expect-jump 0 --expect-sign 0 --expect-app old > "$log_file" 2>&1; then
    printf 'QBOOT_CONFIG_MATRIX_PASS %s\n' "$case_name"; record "$case_name" invalid PASS runtime "$log_file"
  else
    printf 'QBOOT_CONFIG_MATRIX_FAIL %s\n' "$case_name"; cat "$log_file"; record "$case_name" invalid FAIL runtime "$log_file"; exit 1
  fi
}

run_update_mgr_case() {
  local case_name=$1 runner=$2 runner_case=$3
  local log_file="$log_dir/$case_name.log"
  if "$runner" --mode update-mgr --case "$runner_case" > "$log_file" 2>&1; then
    printf 'QBOOT_CONFIG_MATRIX_PASS %s\n' "$case_name"; record "$case_name" valid PASS runtime "$log_file"
  else
    printf 'QBOOT_CONFIG_MATRIX_FAIL %s\n' "$case_name"; cat "$log_file"; record "$case_name" valid FAIL runtime "$log_file"; exit 1
  fi
}

if [ ! -f "$fixture_dir/custom-none-full.rbl" ]; then
  QBOOT_HOST_OUT_DIR="$host_root" QBOOT_HOST_BACKENDS="custom custom-smallbuf fal fs custom-helper custom-hpatch-only fal-hpatch-only fs-hpatch-only" bash .github/ci/qboot/build-host-sim.sh > "$log_dir/fixture-build.log" 2>&1
  QBOOT_HOST_OUT_DIR="$host_root" bash .github/ci/qboot/run-host-sim.sh > "$log_dir/fixture-run.log" 2>&1
fi

run_valid_build config-minimal-custom-only custom-minimal
run_valid_build config-custom-no-compress-no-crypto-no-patch custom-minimal
run_valid_build config-custom-gzip-only custom-gzip-only
run_valid_build config-custom-aes-gzip-only custom-aes-gzip-only
run_valid_build config-custom-hpatch-only custom-hpatch-only
run_valid_build config-fal-only fal-minimal
run_valid_build config-fs-only fs-minimal
run_valid_build config-fal-gzip fal-gzip-only
run_valid_build config-fs-gzip fs-gzip-only
run_valid_build config-all-enabled mixed-backend
run_expected_build_fail config-no-backend-expected-fail none-disabled 'QBT_(APP|DOWNLOAD|FACTORY)_(STORE_NAME|FLASH_ADDR|FLASH_LEN|BACKEND)'
run_valid_build config-multiple-backend-policy mixed-backend
run_update_mgr_case config-mixed-close-fail-rejected "$host_root/qboot_host_runner_mixed-backend" update-helper-close-fail-rejected
run_update_mgr_case config-mixed-close-fail-on-reject-propagated "$host_root/qboot_host_runner_mixed-backend" update-helper-close-fail-on-reject-propagated
run_expected_build_fail config-fal-enabled-without-fal-package-expected-fail fal-missing-package 'undefined reference to .*(fal_|qboot_host_fal_reset)'
run_expected_build_fail config-fs-enabled-without-dfs-expected-fail fs-missing-dfs 'QBOOT filesystem backend requires the RT-Thread DFS package'
run_expected_build_fail config-hpatch-enabled-without-hpatch-lib-expected-fail custom-hpatch-missing-lib 'undefined reference to .*qbt_(hpatchlite_release_from_part|algo_hpatchlite_register)'
run_expected_build_fail config-aes-hpatch-mutual-exclusion-expected-fail custom-aes-hpatch-conflict 'QBOOT_USING_AES and QBOOT_USING_HPATCHLITE cannot be enabled together'
run_expected_build_fail config-aes-enabled-without-tinycrypt-or-aes-lib-expected-fail custom-aes-missing-lib 'undefined reference to .*tiny_aes_(setkey_dec|crypt_cbc)'
run_valid_build config-gzip-disabled-build custom-no-gzip
run_expected_runtime_reject config-gzip-disabled-runtime-rejected "$host_root/qboot_host_runner_custom-no-gzip" "$fixture_dir/custom-gzip.rbl" "$fixture_dir/new_app.bin"
run_valid_build config-aes-disabled-build custom-no-aes
run_expected_runtime_reject config-aes-disabled-runtime-rejected "$host_root/qboot_host_runner_custom-no-aes" "$fixture_dir/custom-aes-gzip-real.rbl" "$fixture_dir/aes_new_app.bin"
run_valid_build config-hpatch-disabled-build custom-no-hpatch
run_expected_runtime_reject config-hpatch-disabled-runtime-rejected "$host_root/qboot_host_runner_custom-no-hpatch" "$fixture_dir/custom-hpatch-host-full-diff.rbl" "$fixture_dir/hpatch_new_app.bin"

{
  printf '# QBoot Host Legal Configuration Matrix\n\nPassed %d/%d matrix checks.\n\n' "$pass_count" "$case_count"
  printf '| Case | Kind | Result | Command | Log |\n|---|---|---:|---|---|\n'
  tail -n +2 "$results" | while IFS=, read -r case_name kind result command_name log_file; do
    printf '| %s | %s | %s | %s | `%s` |\n' "$case_name" "$kind" "$result" "$command_name" "$log_file"
  done
} > "$summary"
printf 'Passed %d/%d QBoot host configuration matrix checks.\n' "$pass_count" "$case_count"
