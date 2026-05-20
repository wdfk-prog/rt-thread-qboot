#!/usr/bin/env bash
set -euo pipefail

out_dir="${QBOOT_HOST_OUT_DIR:-_ci/host-sim}"
fixture_dir="$out_dir/fixtures"
log_dir="$out_dir/logs"
python_bin="${PYTHON:-python3}"
summary="$out_dir/qboot_host_sim_summary.md"
case_list="$out_dir/cases.tsv"
runner_custom="$out_dir/qboot_host_runner_custom"
runner_custom_smallbuf="$out_dir/qboot_host_runner_custom-smallbuf"
runner_custom_hpatch="$out_dir/qboot_host_runner_custom-hpatch-only"
runner_fal="$out_dir/qboot_host_runner_fal"
runner_fal_hpatch="$out_dir/qboot_host_runner_fal-hpatch-only"
runner_fs="$out_dir/qboot_host_runner_fs"
runner_mixed="$out_dir/qboot_host_runner_mixed-backend"
runner_fs_hpatch="$out_dir/qboot_host_runner_fs-hpatch-only"
runner_custom_helper="$out_dir/qboot_host_runner_custom-helper"
export QBOOT_HOST_FIXTURE_DIR="$fixture_dir"
export QBOOT_HOST_OUT_DIR="$out_dir"
. .github/ci/qboot/qboot-host-test-lib.sh

# Keep this runner focused on QBoot release behavior. Do not add CI
# harness self-tests, artifact-presence checks, parser parity checks, or
# generated bookkeeping comparisons here.
mkdir -p "$fixture_dir" "$log_dir"
test -x "$runner_custom"
test -x "$runner_custom_smallbuf"
test -x "$runner_custom_hpatch"
test -x "$runner_fal"
test -x "$runner_fal_hpatch"
test -x "$runner_fs"
test -x "$runner_mixed"
test -x "$runner_fs_hpatch"
test -x "$runner_custom_helper"

PYTHON_BIN="$python_bin" "$python_bin" .github/ci/qboot/generate-host-fixtures.py

case_count=0
pass_count=0
{
  printf '# QBoot L1 Host Upgrade Simulation (%s scope)\n\n' "$(qboot_case_scope_title)"
  printf 'This job validates host-side L1 upgrade paths, fault injection, reset replay, parser consistency, mutation handling, backend mocks, update-manager state tests, fake-flash semantics, and filesystem boundary checks.\n\n'
  printf '## Case matrix\n\n'
  printf '| Backend | Group | Case | Result | Receive | First release | Final release | Jump | Sign | APP | Chunk | Receive mode | Log | Note |\n'
  printf '|---|---|---|---:|---:|---:|---:|---:|---:|---|---:|---|---|---|\n'
} > "$summary"

runner_for_backend() {
  case "$1" in
    custom) printf '%s\n' "$runner_custom" ;;
    custom-smallbuf) printf '%s\n' "$runner_custom_smallbuf" ;;
    custom-hpatch-only) printf '%s\n' "$runner_custom_hpatch" ;;
    fal) printf '%s\n' "$runner_fal" ;;
    fal-hpatch-only) printf '%s\n' "$runner_fal_hpatch" ;;
    fs) printf '%s\n' "$runner_fs" ;;
mixed-backend) printf '%s\n' "$runner_mixed" ;;
    fs-hpatch-only) printf '%s\n' "$runner_fs_hpatch" ;;
    *) echo "unsupported backend: $1" >&2; return 1 ;;
  esac
}

while IFS='|' read -r backend group case_name package new_app_path old_app_path expect_receive expect_first expect_success expect_jump expect_sign expect_app limit chunk receive_mode replay skip_first fail_open fail_read fail_write fail_erase fail_sign_read fail_sign_write fault_before_receive corrupt_sign corrupt_app malloc_fail_after physical_flash fail_sync fail_close note; do
  if ! qboot_case_should_run "$case_name"; then
    continue
  fi
  case_count=$((case_count + 1))
  runner=$(runner_for_backend "$backend")
  log_file="$log_dir/$case_name.log"
  cmd=("$runner" --case "$case_name" --package "$package" --old-app "$old_app_path" --new-app "$new_app_path" --expect-receive "$expect_receive" --expect-first-success "$expect_first" --expect-success "$expect_success" --expect-jump "$expect_jump" --expect-sign "$expect_sign" --expect-app "$expect_app" --chunk "$chunk" --receive-mode "$receive_mode")
  if [ "$limit" != "0" ]; then cmd+=(--download-limit "$limit"); fi
  if [ "$replay" != "0" ]; then cmd+=(--replay "$replay"); fi
  if [ "$skip_first" != "0" ]; then cmd+=(--skip-first-jump "$skip_first"); fi
  if [ "$fault_before_receive" != "0" ]; then cmd+=(--fault-before-receive "$fault_before_receive"); fi
  if [ "$corrupt_sign" != "0" ]; then cmd+=(--corrupt-sign-before-release "$corrupt_sign"); fi
  if [ "$corrupt_app" != "0" ]; then cmd+=(--corrupt-app-before-replay "$corrupt_app"); fi
  if [ -n "$malloc_fail_after" ]; then cmd+=(--malloc-fail-after "$malloc_fail_after"); fi
  if [ "$physical_flash" != "0" ]; then cmd+=(--physical-flash "$physical_flash"); fi
  if [ -n "$fail_open" ]; then cmd+=(--fail-open "$fail_open"); fi
  if [ -n "$fail_read" ]; then cmd+=(--fail-read "$fail_read"); fi
  if [ -n "$fail_write" ]; then cmd+=(--fail-write "$fail_write"); fi
  if [ -n "$fail_erase" ]; then cmd+=(--fail-erase "$fail_erase"); fi
  if [ -n "$fail_sign_read" ]; then cmd+=(--fail-sign-read "$fail_sign_read"); fi
  if [ -n "$fail_sign_write" ]; then cmd+=(--fail-sign-write "$fail_sign_write"); fi
  if [ -n "$fail_close" ]; then cmd+=(--fail-close "$fail_close"); fi
  if "${cmd[@]}" > "$log_file" 2>&1; then
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_CASE_PASS %s\n' "$case_name"
    printf '| %s | %s | %s | PASS | %s | %s | %s | %s | %s | %s | %s | %s | `%s` | %s |\n' "$backend" "$group" "$case_name" "$expect_receive" "$expect_first" "$expect_success" "$expect_jump" "$expect_sign" "$expect_app" "$chunk" "$receive_mode" "$log_file" "$note" >> "$summary"
  else
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$case_name"
    cat "$log_file"
    exit 1
  fi
done < "$case_list"

for update_case in update-mgr-start-finish update-mgr-abort update-mgr-finish-fail update-mgr-register-app-valid update-mgr-register-app-invalid update-mgr-register-in-progress-persists-wait update-mgr-register-req-persists-wait update-mgr-wait-timeout-app-valid-clears-reason update-mgr-wait-timeout-recover-clears-reason update-mgr-idle-timeout-app-valid-clears-reason update-mgr-idle-timeout-recover-clears-reason update-mgr-write-before-start update-mgr-start-twice update-mgr-finish-twice update-mgr-finish-after-abort update-mgr-abort-after-finish concurrent-update-start-rejected callback-null-all callback-reentrant-update-rejected callback-abort-during-progress callback-order-check callback-count-check backend-register-twice update-mgr-write-fail-then-abort update-mgr-write-fail-then-restart update-mgr-abort-clears-download-state receive-abort-after-partial-body-then-restart receive-finish-after-write-error-rejected receive-total-size-size_t-overflow-rejected receive-offset-plus-size-overflow-rejected callback-progress-monotonic callback-progress-final-100-on-success callback-progress-not-100-on-failure callback-error-code-propagated callback-abort-during-sign-phase callback-abort-during-decompress-phase callback-abort-during-hpatch-phase callback-reentrant-finish-rejected update-mgr-register-backend-after-start-rejected update-mgr-register-backend-during-update-rejected update-helper-abort-close-fail-propagated update-helper-ready-close-fail-retries-before-ready update-mgr-start-after-failed-finish update-mgr-start-after-failed-abort update-mgr-finish-callback-reentrant-start-rejected update-mgr-abort-callback-reentrant-start-rejected error-code-update-in-progress error-code-update-not-started update-mgr-partial-write-then-finish-current-policy update-mgr-zero-total-size-current-policy update-mgr-size-mismatch-on-finish-current-policy update-mgr-finish-without-full-body-current-policy update-mgr-unregister-or-replace-backend-current-policy update-mgr-multiple-contexts-current-policy; do
  qboot_case_should_run "$update_case" || continue
  case_count=$((case_count + 1))
  log_file="$log_dir/$update_case.log"
  if "$runner_custom" --mode update-mgr --case "$update_case" > "$log_file" 2>&1; then
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_CASE_PASS %s\n' "$update_case"
    printf '| custom | update-mgr | %s | PASS | - | - | - | - | - | - | - | direct | `%s` | direct update-manager state test |\n' "$update_case" "$log_file" >> "$summary"
  else
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$update_case"
    cat "$log_file"
    exit 1
  fi
done

for helper_case in update-helper-backend-size-smoke update-helper-abort-clears-session update-helper-write-offset-at-size-rejected update-helper-write-offset-plus-size-overflow-rejected update-helper-write-cross-end-rejected update-helper-write-after-finish-rejected update-helper-write-after-abort-rejected update-helper-begin-erase-fail-cleans-handle update-helper-begin-open-fail-keeps-idle; do
  qboot_case_should_run "$helper_case" || continue
  case_count=$((case_count + 1))
  log_file="$log_dir/$helper_case.log"
  if "$runner_custom_helper" --mode update-mgr --case "$helper_case" > "$log_file" 2>&1; then
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_CASE_PASS %s\n' "$helper_case"
    printf '| custom-helper | update-mgr | %s | PASS | - | - | - | - | - | - | - | direct | `%s` | production helper fallback size/abort semantics smoke |\n' "$helper_case" "$log_file" >> "$summary"
  else
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$helper_case"
    cat "$log_file"
    exit 1
  fi
done

for jump_case in jump-disable-irq-check jump-msp-update-check jump-vtor-update-check jump-clear-pending-irq-check jump-deinit-systick-check jump-invalid-stack-pointer jump-invalid-reset-vector jump-vector-table-unaligned jump-stack-pointer-ram-start-boundary jump-stack-pointer-ram-end-boundary jump-reset-vector-thumb-bit-clear-rejected jump-reset-vector-thumb-bit-set-accepted jump-vtor-alignment-cortex-m3-policy jump-vtor-alignment-cortex-m7-policy jump-fpu-state-cleanup-policy jump-cache-barrier-before-branch-policy jump-systick-pending-cleared jump-nvic-enable-bits-cleared; do
  qboot_case_should_run "$jump_case" || continue
  case_count=$((case_count + 1))
  log_file="$log_dir/$jump_case.log"
  if "$runner_custom" --mode jump-stub --case "$jump_case" > "$log_file" 2>&1; then
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_CASE_PASS %s\n' "$jump_case"
    printf '| custom | jump-stub | %s | PASS | - | - | - | - | - | - | - | direct | `%s` | host Cortex-M jump preparation assertion |\n' "$jump_case" "$log_file" >> "$summary"
  else
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$jump_case"
    cat "$log_file"
    exit 1
  fi
done

for fake_case in fake-flash-one-to-zero-only-write fake-flash-write-without-erase-fail fake-flash-sector-unaligned-erase fake-flash-cross-sector-write fake-flash-partition-nonzero-offset fake-flash-neighbor-partition-not-corrupted fake-flash-program-unit-aligned fake-flash-program-unit-unaligned-rejected fake-flash-program-unit-cross-boundary-rejected fake-flash-erase-then-read-all-ff fake-flash-wear-count-not-exceeded-smoke fake-flash-double-erase-idempotent-current-policy fake-flash-program-timeout-current-policy fake-flash-erase-timeout-current-policy; do
  qboot_case_should_run "$fake_case" || continue
  case_count=$((case_count + 1))
  log_file="$log_dir/$fake_case.log"
  if "$runner_custom" --mode fake-flash --case "$fake_case" > "$log_file" 2>&1; then
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_CASE_PASS %s\n' "$fake_case"
    printf '| custom | fake-flash | %s | PASS | - | - | - | - | - | - | - | direct | `%s` | physical fake-flash constraint test |\n' "$fake_case" "$log_file" >> "$summary"
  else
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$fake_case"
    cat "$log_file"
    exit 1
  fi
done

for sign_backend in custom fal; do
  sign_runner=$(runner_for_backend "$sign_backend")
  for sign_case in sign-align-exact sign-align-plus-padding sign-at-partition-end-exact sign-write-cross-sector sign-position-out-of-range sign-erase-does-not-corrupt-app-tail sign-different-pkg-size-rejected sign-same-position-metadata-current-policy; do
    case_name="$sign_backend-$sign_case"
    qboot_case_should_run "$case_name" || continue
    case_count=$((case_count + 1))
    log_file="$log_dir/$case_name.log"
    if "$sign_runner" --mode sign-boundary --case "$sign_case" > "$log_file" 2>&1; then
      pass_count=$((pass_count + 1))
      printf 'QBOOT_HOST_CASE_PASS %s\n' "$case_name"
      printf '| %s | sign-boundary | %s | PASS | - | - | - | - | - | - | - | direct | `%s` | direct release-sign position and isolation test |\n' "$sign_backend" "$sign_case" "$log_file" >> "$summary"
    else
      printf 'QBOOT_HOST_CASE_FAIL %s\n' "$case_name"
      cat "$log_file"
      exit 1
    fi
  done
done

for fs_case in fs-mount-missing fs-read-short-count fs-size-after-truncate-zero fs-close-reopen-readback fs-write-short-count fs-no-space-left fs-path-too-long fs-download-path-readonly fs-sign-path-readonly fs-download-and-sign-same-path fs-stale-temp-file-cleanup fs-existing-sign-file-shorter-than-sign fs-existing-sign-file-longer-than-sign fs-existing-download-file-longer-than-package fs-write-fail-after-download-write-recovery-smoke fs-write-fail-after-download-overwrite-recovery-smoke fs-write-fail-after-download-retry-recovery-smoke fs-temp-file-power-loss-before-rename fs-mount-lost-during-release fs-unmount-before-replay fs-directory-missing-created-or-rejected-policy fs-stale-temp-sign-file-ignored fs-valid-download-survives-close-reopen fs-sign-clear-removes-marker fs-open-app-then-download-independent-fds fs-size-after-sparse-write-reports-logical-size fs-reopen-fail-after-write-current-policy fs-rename-temp-to-download-fail-current-policy fs-mount-lost-after-sign-current-policy; do
  qboot_case_should_run "$fs_case" || continue
  case_count=$((case_count + 1))
  log_file="$log_dir/$fs_case.log"
  if "$runner_fs" --mode fs-boundary --case "$fs_case" > "$log_file" 2>&1; then
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_CASE_PASS %s\n' "$fs_case"
    printf '| fs | fs-boundary | %s | PASS | - | - | - | - | - | - | - | direct | `%s` | direct filesystem boundary test |\n' "$fs_case" "$log_file" >> "$summary"
  else
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$fs_case"
    cat "$log_file"
    exit 1
  fi
done
for mux_case in mux-name-to-id-all-roles mux-open-app-download-factory-dispatches-correct-backend mux-sign-write-goes-to-source-backend-only mux-destination-write-failure-does-not-touch-source mux-release-download-to-custom-app-wrapper-success; do
  qboot_case_should_run "$mux_case" || continue
  case_count=$((case_count + 1))
  log_file="$log_dir/$mux_case.log"
  if "$runner_mixed" --mode mux-contract --case "$mux_case" --fixture-dir "$fixture_dir" > "$log_file" 2>&1; then
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_CASE_PASS %s\n' "$mux_case"
    printf '| mixed-backend | mux-contract | %s | PASS | - | - | - | - | - | - | - | direct | `%s` | direct mux backend dispatch/wrapper test |\n' "$mux_case" "$log_file" >> "$summary"
  else
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$mux_case"
    cat "$log_file"
    exit 1
  fi
done

for boot_case in boot-flow-valid-download-releases-and-jumps-once boot-flow-storage-register-fail-no-jump; do
  qboot_case_should_run "$boot_case" || continue
  case_count=$((case_count + 1))
  log_file="$log_dir/$boot_case.log"
  if "$runner_custom" --mode boot-flow --case "$boot_case" --fixture-dir "$fixture_dir" > "$log_file" 2>&1; then
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_CASE_PASS %s\n' "$boot_case"
    printf '| custom | boot-flow | %s | PASS | - | - | - | - | - | - | - | direct | `%s` | qboot startup-flow orchestration test |\n' "$boot_case" "$log_file" >> "$summary"
  else
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$boot_case"
    cat "$log_file"
    exit 1
  fi
done

for shell_case in shell-cmd-usage-no-args shell-cmd-release-download-success shell-cmd-release-invalid-part-rejected shell-cmd-clone-download-to-factory-byte-exact shell-cmd-verify-corrupt-app-rejected shell-cmd-del-download-clears-package shell-cmd-jump-current-policy; do
  qboot_case_should_run "$shell_case" || continue
  case_count=$((case_count + 1))
  log_file="$log_dir/$shell_case.log"
  if "$runner_custom" --mode shell-cmd --case "$shell_case" --fixture-dir "$fixture_dir" > "$log_file" 2>&1; then
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_CASE_PASS %s\n' "$shell_case"
    printf '| custom | shell-cmd | %s | PASS | - | - | - | - | - | - | - | direct | `%s` | qboot shell command contract test |\n' "$shell_case" "$log_file" >> "$summary"
  else
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$shell_case"
    cat "$log_file"
    exit 1
  fi
done

run_named_release_case() {
  local backend=$1 case_name=$2 package=$3 old_app=$4 new_app=$5 log_file=$6
  shift 6
  local runner
  runner=$(runner_for_backend "$backend")
  "$runner" --case "$case_name" --package "$package" --old-app "$old_app" --new-app "$new_app" "$@" > "$log_file" 2>&1
}

for sweep in \
  "custom-fault-sweep-full-upgrade custom" \
  "fal-fault-sweep-full-upgrade fal" \
  "fs-fault-sweep-full-upgrade fs" \
  "custom-fault-sweep-replay-after-each-op custom" \
  "fal-fault-sweep-replay-after-each-op fal" \
  "fs-fault-sweep-replay-after-each-op fs" \
  "custom-fault-sweep-sign-phase custom" \
  "fal-fault-sweep-sign-phase fal" \
  "fs-fault-sweep-sign-phase fs"; do
  set -- $sweep
  sweep_case=$1
  sweep_backend=$2
  qboot_case_should_run "$sweep_case" || continue
  case_count=$((case_count + 1))
  log_file="$log_dir/$sweep_case.log"
  : > "$log_file"
  if [[ "$sweep_case" == *sign-phase ]]; then
    if [[ "$sweep_backend" == "fal" ]]; then
      specs=("--fail-read download:1" "--fail-write download:0")
    else
      specs=("--fail-sign-read download:0" "--fail-sign-read download:1" "--fail-sign-write download:0")
    fi
  elif [[ "$sweep_case" == *replay-after-each-op ]]; then
    if [[ "$sweep_backend" == "fal" ]]; then
      specs=("--fail-read download:0" "--fail-write app:0" "--fail-write app:1" "--fail-write download:0")
    else
      specs=("--fail-read download:0" "--fail-write app:0" "--fail-write app:1" "--fail-sign-write download:0")
    fi
  else
    if [[ "$sweep_backend" == "fal" ]]; then
      specs=("--fail-read download:0" "--fail-write app:0" "--fail-write app:1" "--fail-erase app:0" "--fail-read download:1" "--fail-write download:0")
    else
      specs=("--fail-read download:0" "--fail-write app:0" "--fail-write app:1" "--fail-erase app:0" "--fail-sign-read download:1" "--fail-sign-write download:0")
    fi
  fi
  sweep_ok=1
  for spec in "${specs[@]}"; do
    read -r opt val <<< "$spec"
    sub_log="$log_dir/$sweep_case-${opt#--}-${val//:/-}.log"
    if [[ "$sweep_case" == *replay-after-each-op && "$sweep_backend" != "fs" ]]; then
      if ! run_named_release_case "$sweep_backend" "$sweep_case-$val" "$fixture_dir/custom-none-full.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/new_app.bin" "$sub_log" --expect-receive 1 --expect-first-success 0 --expect-success 1 --expect-jump 1 --expect-sign 1 --expect-app new --replay true "$opt" "$val"; then
        sweep_ok=0
      fi
    else
      expect_app=old
      expect_first=0
      expect_success=0
      expect_jump=0
      expect_sign=0
      if [[ "$val" == app:* ]]; then expect_app=any; fi
      if [[ "$val" == download:0 && ( "$opt" == --fail-sign-write || "$opt" == --fail-write ) ]]; then expect_app=new; fi
      if [[ "$val" == download:* && "$opt" == --fail-sign-read ]]; then
        expect_app=new
        expect_first=1
        expect_success=1
        expect_jump=1
        expect_sign=1
      fi
      if ! run_named_release_case "$sweep_backend" "$sweep_case-$val" "$fixture_dir/custom-none-full.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/new_app.bin" "$sub_log" --expect-receive 1 --expect-first-success "$expect_first" --expect-success "$expect_success" --expect-jump "$expect_jump" --expect-sign "$expect_sign" --expect-app "$expect_app" "$opt" "$val"; then
        sweep_ok=0
      fi
    fi
    printf '%s %s -> %s\n' "$opt" "$val" "$sub_log" >> "$log_file"
  done
  if [ "$sweep_ok" -eq 1 ]; then
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_CASE_PASS %s\n' "$sweep_case"
    printf '| %s | fault-sweep | %s | PASS | - | - | - | - | - | - | - | sweep | `%s` | deterministic storage failpoint sweep |\n' "$sweep_backend" "$sweep_case" "$log_file" >> "$summary"
  else
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$sweep_case"
    cat "$log_file"
    exit 1
  fi
done

for repeat_case in \
  repeat-upgrade-a-to-b-to-c \
  repeat-upgrade-fail-then-success \
  repeat-upgrade-success-then-stale-download-leftover \
  repeat-upgrade-sign-rewritten-each-time \
  repeat-upgrade-gzip-then-aes; do
  qboot_case_should_run "$repeat_case" || continue
  case_count=$((case_count + 1))
  log_file="$log_dir/$repeat_case.log"
  if "$runner_custom" --mode repeat-sequence --case "$repeat_case" --fixture-dir "$fixture_dir" > "$log_file" 2>&1; then
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_CASE_PASS %s\n' "$repeat_case"
    printf '| custom | repeat | %s | PASS | - | - | - | - | - | - | - | in-process | `%s` | in-process multi-upgrade/stale-state pressure sequence |\n' "$repeat_case" "$log_file" >> "$summary"
  else
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$repeat_case"
    cat "$log_file"
    exit 1
  fi
done

repeat_case=repeat-upgrade-hpatch-then-none
if qboot_case_should_run "$repeat_case"; then
  case_count=$((case_count + 1))
  log_file="$log_dir/$repeat_case.log"
  if "$runner_custom_hpatch" --mode repeat-sequence --case "$repeat_case" --fixture-dir "$fixture_dir" > "$log_file" 2>&1; then
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_CASE_PASS %s\n' "$repeat_case"
    printf '| custom-hpatch-only | repeat | %s | PASS | - | - | - | - | - | - | - | in-process | `%s` | hpatch-only in-process multi-upgrade sequence |\n' "$repeat_case" "$log_file" >> "$summary"
  else
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$repeat_case"
    cat "$log_file"
    exit 1
  fi
fi

repeat_case=repeat-upgrade-switch-backend-state-clean
if qboot_case_should_run "$repeat_case"; then
  case_count=$((case_count + 1))
  log_file="$log_dir/$repeat_case.log"
  : > "$log_file"
  repeat_ok=1
  idx=0
  for item in \
    "custom-none-full.rbl:new_app.bin:old_app.bin:custom" \
    "custom-none-full.rbl:new_app.bin:old_app.bin:fal" \
    "custom-none-full.rbl:new_app.bin:old_app.bin:fs"; do
    IFS=: read -r pkg new old backend <<< "$item"
    sub_log="$log_dir/$repeat_case-$idx.log"
    if ! run_named_release_case "$backend" "$repeat_case-$idx" "$fixture_dir/$pkg" "$fixture_dir/$old" "$fixture_dir/$new" "$sub_log" --expect-receive 1 --expect-first-success 1 --expect-success 1 --expect-jump 1 --expect-sign 1 --expect-app new; then
      repeat_ok=0
    fi
    printf '%s -> %s\n' "$item" "$sub_log" >> "$log_file"
    idx=$((idx + 1))
  done
  if [ "$repeat_ok" -eq 1 ]; then
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_CASE_PASS %s\n' "$repeat_case"
    printf '| custom | backend-switch | %s | PASS | - | - | - | - | - | - | - | independent | `%s` | independent backend state-clean smoke; per-backend process reset is intentional |\n' "$repeat_case" "$log_file" >> "$summary"
  else
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$repeat_case"
    cat "$log_file"
    exit 1
  fi
fi

for parser_case in \
  parser-default-package-inspect-smoke:accept \
  parser-leading-padding-rejected:reject; do
  parser_name=${parser_case%%:*}
  parser_expect=${parser_case#*:}
  qboot_case_should_run "$parser_name" || continue
  case_count=$((case_count + 1))
  log_file="$log_dir/$parser_name.log"
  if [ "$parser_expect" = "reject" ]; then
    padded="$fixture_dir/$parser_name.rbl"
    { printf 'X'; cat "$fixture_dir/custom-none-full.rbl"; } > "$padded"
    if "$runner_custom" --inspect --package "$padded" > "$log_file" 2>&1; then
      printf 'QBOOT_HOST_CASE_FAIL %s\n' "$parser_name"
      cat "$log_file"
      exit 1
    fi
  else
    "$runner_custom" --inspect --package "$fixture_dir/custom-none-full.rbl" > "$log_file" 2>&1
  fi
  pass_count=$((pass_count + 1))
  printf 'QBOOT_HOST_CASE_PASS %s\n' "$parser_name"
  printf '| custom | parser-smoke | %s | PASS | - | - | - | - | - | - | - | inspect | `%s` | parser default inspect or leading-padding rejection smoke case |\n' "$parser_name" "$log_file" >> "$summary"
done


run_extra_release() {
  local backend="custom"
  local success_note="additional success smoke coverage"
  local fail_note="additional rejection smoke coverage"
  if [ "$1" = "--backend" ]; then
    backend=$2
    shift 2
    case "$backend" in
      custom-hpatch-only)
        success_note="additional HPatchLite success smoke coverage"
        fail_note="additional HPatchLite rejection smoke coverage"
        ;;
    esac
  fi

  local case_name=$1 pkg=$2 new_img=$3 expect=${4:-success}
  local log_file="$log_dir/$case_name.log"
  shift 3
  if [ "$#" -gt 0 ] && { [ "$1" = "success" ] || [ "$1" = "fail" ]; }; then
    expect=$1
    shift
  fi
  local extra_args=("$@")
  qboot_case_should_run "$case_name" || return 0
  case_count=$((case_count + 1))
  if [ "$expect" = "fail" ]; then
    if run_named_release_case "$backend" "$case_name" "$pkg" "$fixture_dir/old_app.bin" "$new_img" "$log_file" --expect-receive 1 --expect-first-success 0 --expect-success 0 --expect-jump 0 --expect-sign 0 --expect-app old "${extra_args[@]}"; then
      pass_count=$((pass_count + 1))
      printf 'QBOOT_HOST_CASE_PASS %s\n' "$case_name"
      printf '| %s | policy-smoke | %s | PASS | - | - | - | - | - | - | - | release | `%s` | %s |\n' "$backend" "$case_name" "$log_file" "$fail_note" >> "$summary"
    else
      printf 'QBOOT_HOST_CASE_FAIL %s\n' "$case_name"; cat "$log_file"; exit 1
    fi
  else
    if run_named_release_case "$backend" "$case_name" "$pkg" "$fixture_dir/old_app.bin" "$new_img" "$log_file" --expect-receive 1 --expect-first-success 1 --expect-success 1 --expect-jump 1 --expect-sign 1 --expect-app new "${extra_args[@]}"; then
      pass_count=$((pass_count + 1))
      printf 'QBOOT_HOST_CASE_PASS %s\n' "$case_name"
      printf '| %s | policy-smoke | %s | PASS | - | - | - | - | - | - | - | release | `%s` | %s |\n' "$backend" "$case_name" "$log_file" "$success_note" >> "$summary"
    else
      printf 'QBOOT_HOST_CASE_FAIL %s\n' "$case_name"; cat "$log_file"; exit 1
    fi
  fi
}


# Parser rejection smoke cases.

for parser_extra in \
  "parser-reject-bad-magic-corpus:$fixture_dir/mutation-bad-magic.rbl" \
  "parser-reject-header-crc-corpus:$fixture_dir/mutation-header-crc.rbl" \
  "parser-reject-truncated-header-corpus:$fixture_dir/mutation-truncated-header.rbl" \
  "parser-reject-truncated-body-corpus:$fixture_dir/mutation-truncated-body.rbl" \
  "parser-reject-size-overflow-corpus:$fixture_dir/mutation-pkg-size-too-large.rbl" \
  "parser-reject-unsupported-compress-corpus:$fixture_dir/mutation-unsupported-compress.rbl" \
  "parser-reject-unsupported-crypto-corpus:$fixture_dir/mutation-unsupported-crypto.rbl"; do
  parser_name=${parser_extra%%:*}
  parser_pkg=${parser_extra#*:}
  qboot_case_should_run "$parser_name" || continue
  case_count=$((case_count + 1))
  log_file="$log_dir/$parser_name.log"
  if "$runner_custom" --inspect --package "$parser_pkg" > "$log_file" 2>&1; then
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$parser_name"; cat "$log_file"; exit 1
  fi
  pass_count=$((pass_count + 1))
  printf 'QBOOT_HOST_CASE_PASS %s\n' "$parser_name"
  printf '| custom | parser-reject-smoke | %s | PASS | - | - | - | - | - | - | - | inspect | `%s` | deterministic malformed package is rejected |\n' "$parser_name" "$log_file" >> "$summary"
done

# Metadata, policy, resource, stream, and error-path smoke cases.
run_extra_release metadata-product-match-version-upgrade-target-match "$fixture_dir/custom-version-higher.rbl" "$fixture_dir/new_app.bin"
run_extra_release metadata-product-match-version-downgrade-target-match-current-policy "$fixture_dir/custom-version-lower.rbl" "$fixture_dir/new_app.bin"
run_extra_release metadata-product-mismatch-version-upgrade-rejected "$fixture_dir/custom-product-code-mismatch.rbl" "$fixture_dir/new_app.bin" fail
run_extra_release metadata-product-empty-version-upgrade-rejected "$fixture_dir/custom-product-empty.rbl" "$fixture_dir/new_app.bin" fail
run_extra_release metadata-target-mismatch-product-match-rejected "$fixture_dir/custom-invalid-target.rbl" "$fixture_dir/new_app.bin" fail
run_extra_release metadata-version-max-product-max-target-match-current-policy "$fixture_dir/custom-version-max-field.rbl" "$fixture_dir/new_app.bin"
run_extra_release metadata-version-wraparound-with-product-match-current-policy "$fixture_dir/custom-version-wraparound-current-policy.rbl" "$fixture_dir/new_app.bin"
run_extra_release metadata-product-embedded-nul-with-version-upgrade-current-policy "$fixture_dir/custom-product-code-embedded-nul.rbl" "$fixture_dir/new_app.bin" fail
run_extra_release policy-success-path-smoke "$fixture_dir/custom-none-full.rbl" "$fixture_dir/new_app.bin"
run_extra_release resource-gzip-static-buffer-smoke "$fixture_dir/custom-gzip.rbl" "$fixture_dir/new_app.bin" success --chunk 257
run_extra_release resource-aes-gzip-static-buffer-smoke "$fixture_dir/custom-aes-gzip-real.rbl" "$fixture_dir/aes_new_app.bin" success --chunk 257
run_extra_release resource-app-write-cleanup-smoke "$fixture_dir/custom-none-full.rbl" "$fixture_dir/new_app.bin"
run_extra_release resource-sign-write-cleanup-smoke "$fixture_dir/custom-none-full.rbl" "$fixture_dir/new_app.bin"
run_extra_release stream-1-byte-chunks-package "$fixture_dir/custom-none-full.rbl" "$fixture_dir/new_app.bin" success --chunk 1
run_extra_release stream-prime-sized-chunks-package "$fixture_dir/custom-none-full.rbl" "$fixture_dir/new_app.bin" success --chunk 257
run_extra_release stream-4096-byte-chunks-package "$fixture_dir/custom-none-full.rbl" "$fixture_dir/new_app.bin" success --chunk 4096
run_extra_release stream-large-none-package "$fixture_dir/receive-large-chunk-memory-pressure.rbl" "$fixture_dir/large_chunk_app.bin" success --chunk 65536
run_extra_release stream-gzip-package "$fixture_dir/custom-gzip.rbl" "$fixture_dir/new_app.bin" success --chunk 257
run_extra_release stream-aes-gzip-package "$fixture_dir/custom-aes-gzip-real.rbl" "$fixture_dir/aes_new_app.bin" success --chunk 257
run_extra_release --backend custom-hpatch-only stream-hpatch-full-diff-package "$fixture_dir/custom-hpatch-host-full-diff.rbl" "$fixture_dir/hpatch_new_app.bin" success --chunk 257
run_extra_release policy-reject-malformed-package-smoke "$fixture_dir/mutation-bad-magic.rbl" "$fixture_dir/new_app.bin" fail
run_extra_release policy-reject-product-mismatch-smoke "$fixture_dir/custom-product-code-mismatch.rbl" "$fixture_dir/new_app.bin" fail
run_extra_release error-path-bad-magic-smoke "$fixture_dir/mutation-bad-magic.rbl" "$fixture_dir/new_app.bin" fail
run_extra_release error-path-header-crc-smoke "$fixture_dir/mutation-header-crc.rbl" "$fixture_dir/new_app.bin" fail
run_extra_release error-path-raw-crc-smoke "$fixture_dir/mutation-raw-crc.rbl" "$fixture_dir/new_app.bin" fail
run_extra_release error-path-product-mismatch-smoke "$fixture_dir/custom-product-code-mismatch.rbl" "$fixture_dir/new_app.bin" fail
run_extra_release error-path-target-mismatch-smoke "$fixture_dir/custom-invalid-target.rbl" "$fixture_dir/new_app.bin" fail
run_extra_release error-path-target-too-small-smoke "$fixture_dir/target-size-plus-one.rbl" "$fixture_dir/plus_one_app.bin" fail
run_extra_release --backend custom-hpatch-only resource-hpatch-malloc-fail-smoke "$fixture_dir/custom-hpatch-host-full-diff.rbl" "$fixture_dir/hpatch_new_app.bin" fail --malloc-fail-after 0
run_extra_release policy-success-final-smoke "$fixture_dir/custom-none-full.rbl" "$fixture_dir/new_app.bin"

# Fault replay and FAL layout smoke cases.
for mf_case in custom-fault-replay-write-app fal-fault-replay-write-app fs-fault-replay-read-download custom-reset-app-write-replay fal-reset-app-write-replay fs-reset-read-download-replay custom-repeat-replay-smoke fal-repeat-replay-smoke fs-repeat-replay-smoke custom-fail-sequence-smoke fal-fail-sequence-smoke fs-fail-sequence-smoke; do
  backend=${mf_case%%-*}
  qboot_case_should_run "$mf_case" || continue
  case_count=$((case_count + 1))
  log_file="$log_dir/$mf_case.log"
  fault_args=(--fail-write app:1)
  if [ "$backend" = "fs" ]; then
    fault_args=(--fail-read download:1)
  fi
  if run_named_release_case "$backend" "$mf_case" "$fixture_dir/custom-none-full.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/new_app.bin" "$log_file" --expect-receive 1 --expect-first-success 0 --expect-success 1 --expect-jump 1 --expect-sign 1 --expect-app new --replay true "${fault_args[@]}"; then
    pass_count=$((pass_count + 1)); printf 'QBOOT_HOST_CASE_PASS %s\n' "$mf_case"
    printf '| %s | fault-replay-smoke | %s | PASS | - | - | - | - | - | - | - | replay | `%s` | deterministic single-fault replay smoke sequence |\n' "$backend" "$mf_case" "$log_file" >> "$summary"
  else
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$mf_case"; cat "$log_file"; exit 1
  fi
done
for fal_layout_case in \
  "fal-default-layout-release-smoke:$fixture_dir/custom-none-full.rbl:success" \
  "fal-default-layout-malformed-header-reject-smoke:$fixture_dir/mutation-truncated-header.rbl:fail"; do
  fal_case_name=${fal_layout_case%%:*}
  fal_rest=${fal_layout_case#*:}
  fal_pkg=${fal_rest%:*}
  fal_expect=${fal_rest##*:}
  qboot_case_should_run "$fal_case_name" || continue
  case_count=$((case_count + 1))
  log_file="$log_dir/$fal_case_name.log"
  if [ "$fal_expect" = "fail" ]; then
    run_named_release_case fal "$fal_case_name" "$fal_pkg" "$fixture_dir/old_app.bin" "$fixture_dir/new_app.bin" "$log_file" --expect-receive 1 --expect-first-success 0 --expect-success 0 --expect-jump 0 --expect-sign 0 --expect-app old
  else
    run_named_release_case fal "$fal_case_name" "$fal_pkg" "$fixture_dir/old_app.bin" "$fixture_dir/new_app.bin" "$log_file" --expect-receive 1 --expect-first-success 1 --expect-success 1 --expect-jump 1 --expect-sign 1 --expect-app new
  fi
  pass_count=$((pass_count + 1))
  printf 'QBOOT_HOST_CASE_PASS %s\n' "$fal_case_name"
  printf '| fal | fal-default-layout-smoke | %s | PASS | - | - | - | - | - | - | - | release | `%s` | FAL default-layout release/rejection smoke case |\n' "$fal_case_name" "$log_file" >> "$summary"
done

printf '\n## Parser rejection smoke\n\nPASS: malformed magic, header CRC, truncated header/body, size-overflow, and unsupported-algorithm packages are rejected by the C parser.\n' >> "$summary"

{
  printf '\n## Notes\n\n'
  printf -- '- Version downgrade is accepted by the current no-anti-rollback policy; extra-tail remains documented as current package-size policy.\n'
  printf -- '- Receive, gzip, and AES paths use static buffers in production; resource smoke tests only exercise the HPatchLite host full-diff allocation path where dynamic allocation exists.\n'
  printf -- '- Fake-flash cases model 1-to-0 writes and sector-aligned erase rules; they do not replace board-level Flash validation.\n'
  printf '\nPassed %d/%d L1 host simulation cases.\n' "$pass_count" "$case_count"
} >> "$summary"

if [ "$pass_count" -ne "$case_count" ]; then
  printf 'L1 host simulation passed %d/%d cases\n' "$pass_count" "$case_count" >&2
  exit 1
fi
printf 'Passed %d/%d L1 host simulation cases.\n' "$pass_count" "$case_count"
printf 'All L1 qboot host simulation cases passed.\n'
