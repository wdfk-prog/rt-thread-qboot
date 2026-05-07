#!/usr/bin/env bash
set -euo pipefail

out_dir="_ci/host-sim"
fixture_dir="$out_dir/fixtures"
log_dir="$out_dir/extended-logs"
summary="$out_dir/extended_coverage_summary.md"
python_bin="${PYTHON:-python3}"
runner_custom="$out_dir/qboot_host_runner_custom"
runner_fal="$out_dir/qboot_host_runner_fal"
runner_fs="$out_dir/qboot_host_runner_fs"
runner_hpatch="$out_dir/qboot_host_runner_custom-hpatch-production"
runner_hpatch_storage="$out_dir/qboot_host_runner_custom-hpatch-storage-swap"

mkdir -p "$log_dir"
for runner in "$runner_custom" "$runner_fal" "$runner_fs"; do
  test -x "$runner"
done
test -f "$fixture_dir/custom-none-full.rbl"
pass_count=0
case_count=0
skip_count=0
{
  printf '# QBoot Host Extended Coverage\n\n'
  printf '| Case | Result | Log | Note |\n|---|---:|---|---|\n'
} > "$summary"

fail_case() {
  printf 'QBOOT_HOST_EXTENDED_FAIL %s\n' "$1" >&2
  cat "$2" >&2
  exit 1
}

pass_case() {
  pass_count=$((pass_count + 1))
  printf 'QBOOT_HOST_EXTENDED_PASS %s\n' "$1"
  printf '| %s | PASS | `%s` | %s |\n' "$1" "$2" "$3" >> "$summary"
}

skip_case() {
  skip_count=$((skip_count + 1))
  printf 'QBOOT_HOST_EXTENDED_SKIP %s %s\n' "$1" "$2"
  printf '| %s | SKIP | - | %s |\n' "$1" "$2" >> "$summary"
}

run_release() {
  local runner=$1 case_name=$2 pkg=$3 old=$4 new=$5 expect=$6 log_file
  shift 6
  log_file="$log_dir/$case_name.log"
  case_count=$((case_count + 1))
  if [ "$expect" = "success" ]; then
    "$runner" --case "$case_name" --package "$pkg" --old-app "$old" --new-app "$new" \
      --expect-receive 1 --expect-first-success 1 --expect-success 1 \
      --expect-jump 1 --expect-sign 1 --expect-app new "$@" > "$log_file" 2>&1 || fail_case "$case_name" "$log_file"
    pass_case "$case_name" "$log_file" "release success"
  else
    "$runner" --case "$case_name" --package "$pkg" --old-app "$old" --new-app "$new" \
      --expect-receive 1 --expect-first-success 0 --expect-success 0 \
      --expect-jump 0 --expect-sign 0 --expect-app old "$@" > "$log_file" 2>&1 || fail_case "$case_name" "$log_file"
    pass_case "$case_name" "$log_file" "release rejection"
  fi
}

hpatch_production_available=0
if [ -x "$runner_hpatch" ] && [ -f "$fixture_dir/custom-hpatch-production-full.rbl" ] && \
   [ -f "$fixture_dir/custom-hpatch-production-delta.rbl" ] && \
   [ -f "$fixture_dir/mutation-hpatch-production-output-too-large.rbl" ]; then
  hpatch_production_available=1
  run_release "$runner_hpatch" hpatch-production-full-diff "$fixture_dir/custom-hpatch-production-full.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/hpatch_new_app.bin" success --chunk 257
  run_release "$runner_hpatch" hpatch-production-old-dependent-delta "$fixture_dir/custom-hpatch-production-delta.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/hpatch_new_app.bin" success --chunk 257
  run_release "$runner_hpatch" hpatch-production-output-plus-one-rejected "$fixture_dir/mutation-hpatch-production-output-too-large.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/plus_one_app.bin" fail --chunk 257
elif [ "${QBOOT_HOST_ALLOW_HPATCHLITE_SKIP:-0}" = "1" ]; then
  skip_case hpatch-production-real-lib "production HPatchLite runner or fixtures are missing in dependency bootstrap mode"
else
  printf 'QBOOT_HOST_EXTENDED_FAIL hpatch-production-real-lib\n' >&2
  printf 'Production HPatchLite runner or fixtures are missing; set QBOOT_HOST_ALLOW_HPATCHLITE_SKIP=1 only for dependency bootstrap jobs.\n' >&2
  exit 1
fi
if [ "$hpatch_production_available" = "1" ]; then
  run_release "$runner_hpatch" resource-hpatch-production-malloc-first-fails "$fixture_dir/custom-hpatch-production-full.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/hpatch_new_app.bin" fail --chunk 257 --malloc-fail-after 0
  run_release "$runner_hpatch" resource-hpatch-production-malloc-after-first-succeeds "$fixture_dir/custom-hpatch-production-full.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/hpatch_new_app.bin" success --chunk 257 --malloc-fail-after 1
  if [ -x "$runner_hpatch_storage" ]; then
    run_release "$runner_hpatch_storage" hpatch-storage-swap-full-diff "$fixture_dir/custom-hpatch-production-full.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/hpatch_new_app.bin" success --chunk 257
    case_name=resource-hpatch-storage-swap-close-fail-rejected
    log_file="$log_dir/$case_name.log"
    case_count=$((case_count + 1))
    "$runner_hpatch_storage" --case "$case_name" \
      --package "$fixture_dir/custom-hpatch-production-full.rbl" \
      --old-app "$fixture_dir/old_app.bin" \
      --new-app "$fixture_dir/hpatch_new_app.bin" \
      --expect-receive 1 --expect-first-success 0 --expect-success 0 \
      --expect-jump 0 --expect-sign 0 --expect-app new \
      --chunk 257 --fail-close swap:0 > "$log_file" 2>&1 || fail_case "$case_name" "$log_file"
    pass_case "$case_name" "$log_file" "storage swap close failure is propagated after APP write"
  else
    skip_case hpatch-storage-swap-close-fail "storage-swap HPatchLite runner is missing"
  fi
fi
run_release "$runner_custom" resource-hpatch-host-adapter-malloc-after-first-succeeds "$fixture_dir/custom-hpatch-host-full-diff.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/hpatch_new_app.bin" success --chunk 257 --malloc-fail-after 1

for item in \
  "protocol-total-size-short-rejected-before-finish:protocol-total-size-short" \
  "protocol-total-size-long-rejected-before-finish:protocol-total-size-long" \
  "protocol-gap-rejected:protocol-gap-rejected" \
  "protocol-overlap-rejected:protocol-overlap-rejected" \
  "protocol-duplicate-different-data-rejected:protocol-duplicate-different-data"; do
  case_name=${item%%:*}
  mode=${item#*:}
  log_file="$log_dir/$case_name.log"
  case_count=$((case_count + 1))
  "$runner_custom" --case "$case_name" --package "$fixture_dir/custom-none-full.rbl" \
    --old-app "$fixture_dir/old_app.bin" --new-app "$fixture_dir/new_app.bin" \
    --expect-receive 0 --expect-first-success 0 --expect-success 0 \
    --expect-jump 0 --expect-sign 0 --expect-app old --receive-mode "$mode" --chunk 97 \
    > "$log_file" 2>&1 || fail_case "$case_name" "$log_file"
  pass_case "$case_name" "$log_file" "strict protocol adapter rejects invalid transfer metadata"
done
run_release "$runner_custom" protocol-resume-after-reset-continues-from-offset "$fixture_dir/custom-none-full.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/new_app.bin" success --receive-mode protocol-resume-after-reset --chunk 97

"$python_bin" - "$fixture_dir" <<'PYFUZZ'
from pathlib import Path
import random, struct, sys, zlib
root = Path(sys.argv[1])
base = bytearray((root / 'custom-none-full.rbl').read_bytes())
rng = random.Random(20260505)
header_size = 96
for idx in range(24):
    data = bytearray(base)
    kind = idx % 8
    if kind == 0:
        data[rng.randrange(0, header_size)] ^= rng.randrange(1, 256)
    elif kind == 1:
        data[88:92] = struct.pack('<I', len(data) + rng.randrange(1, 4096))
    elif kind == 2:
        data[84:88] = struct.pack('<I', rng.randrange(0, 0x80000))
    elif kind == 3:
        data[4:6] = struct.pack('<H', (rng.randrange(6, 128) << 8) | rng.randrange(0, 8))
    elif kind == 4:
        data[header_size + rng.randrange(0, len(data) - header_size)] ^= rng.randrange(1, 256)
        data[76:80] = struct.pack('<I', zlib.crc32(bytes(data[header_size:])) & 0xFFFFFFFF)
        data[80:84] = struct.pack('<I', rng.randrange(1, 0xFFFFFFFF))
    elif kind == 5:
        del data[header_size + rng.randrange(1, len(data) - header_size):]
    elif kind == 6:
        data[52:76] = b'X' * 24
    else:
        data.extend(bytes(rng.randrange(256) for _ in range(17)))
        data[88:92] = struct.pack('<I', 0)
    (root / f'parser-fuzz-seed-{idx:04d}.rbl').write_bytes(data)
PYFUZZ
for fuzz_pkg in "$fixture_dir"/parser-fuzz-seed-*.rbl; do
  fuzz_name=${fuzz_pkg##*/}
  fuzz_name=${fuzz_name%.rbl}
  log_file="$log_dir/$fuzz_name.log"
  case_count=$((case_count + 1))
  if "$runner_custom" --inspect --package "$fuzz_pkg" > "$log_file" 2>&1; then
    fail_case "$fuzz_name" "$log_file"
  fi
  pass_case "$fuzz_name" "$log_file" "parser fuzz package rejected safely"
done

parser_property_manifest="$fixture_dir/parser-property-manifest.tsv"
cat > "$parser_property_manifest" <<EOF_PROP
accept	parser-property-valid-none	custom-none-full.rbl	none package fields round-trip
accept	parser-property-valid-gzip	custom-gzip.rbl	gzip package fields round-trip
accept	parser-property-valid-aes-gzip	custom-aes-gzip-real.rbl	AES+gzip package fields round-trip
accept	parser-property-valid-hpatch	custom-hpatch-host-full-diff.rbl	HPatchLite package fields round-trip
accept	parser-property-valid-target-exact-fit	target-size-exact-fit.rbl	maximum raw size accepted by parser
accept	parser-property-valid-target-minus-one	target-size-minus-one.rbl	boundary raw size accepted by parser
accept	parser-property-valid-product-no-nul	custom-product-code-no-nul.rbl	non-NUL product field remains parseable
accept	parser-property-valid-version-max	custom-version-max-field.rbl	max version field remains parseable
reject	parser-property-reject-bad-magic	mutation-bad-magic.rbl	bad magic rejected
reject	parser-property-reject-header-crc	mutation-header-crc.rbl	header CRC rejected
reject	parser-property-reject-truncated-header	mutation-truncated-header.rbl	truncated header rejected
reject	parser-property-reject-truncated-body	mutation-truncated-body.rbl	truncated body rejected
reject	parser-property-reject-unsupported-compress	mutation-unsupported-compress.rbl	unsupported compression rejected
reject	parser-property-reject-unsupported-crypto	mutation-unsupported-crypto.rbl	unsupported crypto rejected
reject	parser-property-reject-size-overflow	mutation-pkg-size-too-large.rbl	declared package body overflow rejected
EOF_PROP
while IFS=$'	' read -r prop_expect prop_name prop_pkg prop_note; do
  [ -n "$prop_expect" ] || continue
  log_file="$log_dir/$prop_name.log"
  case_count=$((case_count + 1))
  if [ "$prop_expect" = "accept" ]; then
    "$runner_custom" --inspect --package "$fixture_dir/$prop_pkg" > "$log_file" 2>&1 || fail_case "$prop_name" "$log_file"
    pass_case "$prop_name" "$log_file" "$prop_note"
  else
    if "$runner_custom" --inspect --package "$fixture_dir/$prop_pkg" > "$log_file" 2>&1; then
      fail_case "$prop_name" "$log_file"
    fi
    pass_case "$prop_name" "$log_file" "$prop_note"
  fi
done < "$parser_property_manifest"

for item in \
  "stream-aes-gzip-chunk-15:custom-aes-gzip-real.rbl:aes_new_app.bin:15" \
  "stream-aes-gzip-chunk-16:custom-aes-gzip-real.rbl:aes_new_app.bin:16" \
  "stream-aes-gzip-chunk-17:custom-aes-gzip-real.rbl:aes_new_app.bin:17" \
  "stream-aes-gzip-chunk-31:custom-aes-gzip-real.rbl:aes_new_app.bin:31" \
  "stream-aes-gzip-chunk-32:custom-aes-gzip-real.rbl:aes_new_app.bin:32" \
  "stream-hpatch-cross-chunk-15:custom-hpatch-host-full-diff.rbl:hpatch_new_app.bin:15" \
  "stream-hpatch-cross-chunk-17:custom-hpatch-host-full-diff.rbl:hpatch_new_app.bin:17"; do
  case_name=${item%%:*}
  rest=${item#*:}
  pkg=${rest%%:*}
  rest=${rest#*:}
  new_img=${rest%%:*}
  chunk=${rest##*:}
  run_release "$runner_custom" "$case_name" "$fixture_dir/$pkg" "$fixture_dir/old_app.bin" "$fixture_dir/$new_img" success --chunk "$chunk"
done

for item in \
  "multi-fault-app-write-then-replay:custom:--fail-write app:1" \
  "multi-fault-download-read-then-replay:custom:--fail-read download:0" \
  "multi-fault-fal-app-write-then-replay:fal:--fail-write app:1" \
  "multi-fault-fs-read-then-replay:fs:--fail-read download:1"; do
  case_name=${item%%:*}
  rest=${item#*:}
  backend=${rest%%:*}
  args=${rest#*:}
  runner="$runner_custom"
  [ "$backend" = "fal" ] && runner="$runner_fal"
  [ "$backend" = "fs" ] && runner="$runner_fs"
  log_file="$log_dir/$case_name.log"
  case_count=$((case_count + 1))
  # shellcheck disable=SC2086
  "$runner" --case "$case_name" --package "$fixture_dir/custom-none-full.rbl" \
    --old-app "$fixture_dir/old_app.bin" --new-app "$fixture_dir/new_app.bin" \
    --expect-receive 1 --expect-first-success 0 --expect-success 1 \
    --expect-jump 1 --expect-sign 1 --expect-app new --replay true $args \
    > "$log_file" 2>&1 || fail_case "$case_name" "$log_file"
  pass_case "$case_name" "$log_file" "single-fault replay converges to a valid new app"
done

for fault_sequence_case in fault-sequence-erase-write-sign-success fault-sequence-read-write-signread-success; do
  for item in "custom:$runner_custom" "fal:$runner_fal"; do
    backend=${item%%:*}
    runner=${item#*:}
    case_name="$backend-$fault_sequence_case"
    log_file="$log_dir/$case_name.log"
    case_count=$((case_count + 1))
    "$runner" --mode fault-sequence --case "$fault_sequence_case" --fixture-dir "$fixture_dir" > "$log_file" 2>&1 || fail_case "$case_name" "$log_file"
    pass_case "$case_name" "$log_file" "deterministic multi-fault replay sequence converges after injected reset-class faults"
  done
done

for fake_case in fake-flash-partition-nonzero-offset fake-flash-neighbor-partition-not-corrupted fake-flash-cross-sector-write; do
  log_file="$log_dir/fal-layout-$fake_case.log"
  case_count=$((case_count + 1))
  "$runner_custom" --mode fake-flash --case "$fake_case" > "$log_file" 2>&1 || fail_case "fal-layout-$fake_case" "$log_file"
  pass_case "fal-layout-$fake_case" "$log_file" "host partition offset/neighbor-boundary contract"
done
for fs_case in fs-rename-temp-to-download-fail-current-policy fs-temp-file-power-loss-before-rename fs-stale-temp-sign-file-ignored fs-write-fail-after-download-retry-current-policy fs-close-fail-current-policy; do
  log_file="$log_dir/$fs_case.log"
  case_count=$((case_count + 1))
  "$runner_fs" --mode fs-boundary --case "$fs_case" > "$log_file" 2>&1 || fail_case "$fs_case" "$log_file"
  pass_case "$fs_case" "$log_file" "filesystem atomicity/current-policy"
done
for update_case in callback-progress-monotonic callback-progress-final-100-on-success callback-error-code-propagated error-code-update-in-progress error-code-update-not-started; do
  log_file="$log_dir/$update_case.log"
  case_count=$((case_count + 1))
  "$runner_custom" --mode update-mgr --case "$update_case" > "$log_file" 2>&1 || fail_case "$update_case" "$log_file"
  pass_case "$update_case" "$log_file" "progress/error-code contract"
done

printf '\nPassed %d/%d QBoot host extended coverage checks; skipped %d optional checks.\n' "$pass_count" "$case_count" "$skip_count" >> "$summary"
printf 'Passed %d/%d QBoot host extended coverage checks; skipped %d optional checks.\n' "$pass_count" "$case_count" "$skip_count"
