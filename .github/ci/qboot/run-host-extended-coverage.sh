#!/usr/bin/env bash
set -euo pipefail

out_dir="${QBOOT_HOST_OUT_DIR:-_ci/host-sim}"
fixture_dir="$out_dir/fixtures"
log_dir="$out_dir/extended-logs"
summary="$out_dir/extended_coverage_summary.md"
python_bin="${PYTHON:-python3}"
runner_custom="$out_dir/qboot_host_runner_custom"
runner_fal="$out_dir/qboot_host_runner_fal"
runner_fs="$out_dir/qboot_host_runner_fs"
runner_quickfast="$out_dir/qboot_host_runner_custom-quicklz-fastlz"
runner_mixed="$out_dir/qboot_host_runner_mixed-backend"
runner_fal_hpatch="$out_dir/qboot_host_runner_fal-hpatch-only"
runner_fs_hpatch="$out_dir/qboot_host_runner_fs-hpatch-only"
runner_fal_hpatch_prod="$out_dir/qboot_host_runner_fal-hpatch-production"
runner_fs_hpatch_prod="$out_dir/qboot_host_runner_fs-hpatch-production"
runner_hpatch_host="$out_dir/qboot_host_runner_custom-hpatch-only"
runner_hpatch="$out_dir/qboot_host_runner_custom-hpatch-production"
runner_hpatch_storage="$out_dir/qboot_host_runner_custom-hpatch-storage-swap"

mkdir -p "$log_dir"
for runner in "$runner_custom" "$runner_fal" "$runner_fs" "$runner_quickfast" "$runner_mixed" "$runner_fal_hpatch" "$runner_fs_hpatch" "$runner_hpatch_host"; do
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
  case_count=$((case_count + 1))
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
   [ -f "$fixture_dir/custom-hpatch-production-tuz.rbl" ] && \
   [ -f "$fixture_dir/custom-hpatch-production-multi-cover.rbl" ] && \
   [ -f "$fixture_dir/custom-hpatch-production-multi-cover-tuz.rbl" ] && \
   [ -f "$fixture_dir/mutation-hpatch-production-multi-cover-truncated.rbl" ] && \
   [ -f "$fixture_dir/mutation-hpatch-production-tuz-trailing-byte.rbl" ] && \
   [ -f "$fixture_dir/mutation-hpatch-production-output-too-large.rbl" ] && \
   [ -f "$fixture_dir/mutation-hpatch-production-raw-crc.rbl" ]; then
  hpatch_production_available=1
  run_release "$runner_hpatch" hpatch-production-full-diff "$fixture_dir/custom-hpatch-production-full.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/hpatch_new_app.bin" success --chunk 257
  run_release "$runner_hpatch" hpatch-production-old-dependent-delta "$fixture_dir/custom-hpatch-production-delta.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/hpatch_new_app.bin" success --chunk 257
  run_release "$runner_hpatch" hpatch-production-tinyuz-full-diff "$fixture_dir/custom-hpatch-production-tuz.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/hpatch_new_app.bin" success --chunk 257
  run_release "$runner_hpatch" hpatch-production-multi-cover "$fixture_dir/custom-hpatch-production-multi-cover.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/hpatch_multi_new_app.bin" success --chunk 257
  run_release "$runner_hpatch" hpatch-production-multi-cover-tinyuz "$fixture_dir/custom-hpatch-production-multi-cover-tuz.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/hpatch_multi_new_app.bin" success --chunk 257
  run_release "$runner_hpatch" hpatch-production-multi-cover-truncated-rejected "$fixture_dir/mutation-hpatch-production-multi-cover-truncated.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/hpatch_multi_new_app.bin" fail --chunk 257
  run_release "$runner_hpatch" hpatch-production-tinyuz-trailing-byte-rejected "$fixture_dir/mutation-hpatch-production-tuz-trailing-byte.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/hpatch_new_app.bin" fail --chunk 257
  run_release "$runner_hpatch" hpatch-production-output-plus-one-rejected "$fixture_dir/mutation-hpatch-production-output-too-large.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/plus_one_app.bin" fail --chunk 257
  case_name=hpatch-production-raw-crc-dest-verify-rejected
  log_file="$log_dir/$case_name.log"
  case_count=$((case_count + 1))
  "$runner_hpatch" --case "$case_name" \
    --package "$fixture_dir/mutation-hpatch-production-raw-crc.rbl" \
    --old-app "$fixture_dir/old_app.bin" \
    --new-app "$fixture_dir/hpatch_new_app.bin" \
    --expect-receive 1 --expect-first-success 0 --expect-success 0 \
    --expect-jump 0 --expect-sign 0 --expect-app new \
    --chunk 257 > "$log_file" 2>&1 || fail_case "$case_name" "$log_file"
  pass_case "$case_name" "$log_file" "destination APP raw CRC rejects HPatchLite output after restore"
  if [ -x "$runner_fal_hpatch_prod" ]; then
    run_release "$runner_fal_hpatch_prod" fal-hpatch-production-multi-cover "$fixture_dir/custom-hpatch-production-multi-cover.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/hpatch_multi_new_app.bin" success --chunk 257
    run_release "$runner_fal_hpatch_prod" fal-hpatch-production-tinyuz-trailing-byte-rejected "$fixture_dir/mutation-hpatch-production-tuz-trailing-byte.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/hpatch_new_app.bin" fail --chunk 257
  else
    skip_case fal-hpatch-production-multi-cover "FAL production HPatchLite runner is missing"
    skip_case fal-hpatch-production-tinyuz-trailing-byte-rejected "FAL production HPatchLite runner is missing"
  fi
  if [ -x "$runner_fs_hpatch_prod" ]; then
    run_release "$runner_fs_hpatch_prod" fs-hpatch-production-multi-cover "$fixture_dir/custom-hpatch-production-multi-cover.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/hpatch_multi_new_app.bin" success --chunk 257
    run_release "$runner_fs_hpatch_prod" fs-hpatch-production-tinyuz-trailing-byte-rejected "$fixture_dir/mutation-hpatch-production-tuz-trailing-byte.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/hpatch_new_app.bin" fail --chunk 257
  else
    skip_case fs-hpatch-production-multi-cover "FS production HPatchLite runner is missing"
    skip_case fs-hpatch-production-tinyuz-trailing-byte-rejected "FS production HPatchLite runner is missing"
  fi
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
  run_release "$runner_hpatch" resource-hpatch-production-tinyuz-malloc-first-fails "$fixture_dir/custom-hpatch-production-tuz.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/hpatch_new_app.bin" fail --chunk 257 --malloc-fail-after 0
  run_release "$runner_hpatch" resource-hpatch-production-tinyuz-malloc-after-first-succeeds "$fixture_dir/custom-hpatch-production-tuz.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/hpatch_new_app.bin" success --chunk 257 --malloc-fail-after 1
  if [ -x "$runner_hpatch_storage" ]; then
    run_release "$runner_hpatch_storage" hpatch-storage-swap-full-diff "$fixture_dir/custom-hpatch-production-full.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/hpatch_new_app.bin" success --chunk 257
    run_release "$runner_hpatch_storage" hpatch-storage-swap-old-dependent-delta "$fixture_dir/custom-hpatch-production-delta.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/hpatch_new_app.bin" success --chunk 257
    run_release "$runner_hpatch_storage" hpatch-storage-swap-multi-cover "$fixture_dir/custom-hpatch-production-multi-cover.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/hpatch_multi_new_app.bin" success --chunk 257
    run_release "$runner_hpatch_storage" hpatch-storage-swap-truncated-rejected "$fixture_dir/mutation-hpatch-production-multi-cover-truncated.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/hpatch_multi_new_app.bin" fail --chunk 257
    run_release "$runner_hpatch_storage" hpatch-storage-swap-output-plus-one-rejected "$fixture_dir/mutation-hpatch-production-output-too-large.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/plus_one_app.bin" fail --chunk 257
    case_name=hpatch-storage-swap-reset-during-swap-write-replay
    log_file="$log_dir/$case_name.log"
    case_count=$((case_count + 1))
    "$runner_hpatch_storage" --case "$case_name" \
      --package "$fixture_dir/custom-hpatch-production-full.rbl" \
      --old-app "$fixture_dir/old_app.bin" \
      --new-app "$fixture_dir/hpatch_new_app.bin" \
      --expect-receive 1 --expect-first-success 0 --expect-success 1 \
      --expect-jump 1 --expect-sign 1 --expect-app new \
      --chunk 257 --replay true --fail-write swap:0 > "$log_file" 2>&1 || fail_case "$case_name" "$log_file"
    pass_case "$case_name" "$log_file" "storage swap write fault recovers by replay"
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
    for skipped_case in \
      hpatch-storage-swap-full-diff \
      hpatch-storage-swap-old-dependent-delta \
      hpatch-storage-swap-multi-cover \
      hpatch-storage-swap-truncated-rejected \
      hpatch-storage-swap-output-plus-one-rejected \
      hpatch-storage-swap-reset-during-swap-write-replay \
      resource-hpatch-storage-swap-close-fail-rejected; do
      skip_case "$skipped_case" "storage-swap HPatchLite runner is missing"
    done
  fi
fi
run_release "$runner_hpatch_host" resource-hpatch-host-adapter-malloc-after-first-succeeds "$fixture_dir/custom-hpatch-host-full-diff.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/hpatch_new_app.bin" success --chunk 257 --malloc-fail-after 1

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

$python_bin - "$fixture_dir" <<'PYNAMEDFUZZ'
from pathlib import Path
import struct, zlib, sys
root = Path(sys.argv[1])
hdr = 96
ALGO_OFF = 4
FW_VER_OFF = 28
PROD_OFF = 52
PKG_CRC_OFF = 76
RAW_CRC_OFF = 80
RAW_SIZE_OFF = 84
PKG_SIZE_OFF = 88
HDR_CRC_OFF = 92
def crc32(data): return zlib.crc32(data) & 0xffffffff
def write_u32(data, off, value): data[off:off + 4] = struct.pack('<I', value)
def refresh_hdr_crc(data): write_u32(data, HDR_CRC_OFF, crc32(bytes(data[:HDR_CRC_OFF])))
def update_body(data, body):
    del data[hdr:]
    data.extend(body)
    write_u32(data, PKG_CRC_OFF, crc32(body))
    write_u32(data, PKG_SIZE_OFF, len(body))
    refresh_hdr_crc(data)
def write_from(base_name, name, edit):
    data = bytearray((root / base_name).read_bytes())
    edit(data)
    (root / f'{name}.rbl').write_bytes(data)
def mutate_hpatch_body_varint(data):
    code = data[hdr + 3]
    new_size_len = code & 0x07
    uncompress_size_len = (code >> 3) & 0x07
    body_off = hdr + 4 + new_size_len + uncompress_size_len
    if body_off >= len(data):
        raise RuntimeError('HPatchLite body is missing')
    # Corrupt the first body varint after the HPatchLite header. This keeps the
    # HPatchLite magic, compression type, code field, and size fields valid so
    # the runner exercises body-varint parsing instead of header rejection.
    data[body_off:body_off + 4] = b'\xff\xff\xff\xff'
    write_u32(data, PKG_CRC_OFF, crc32(bytes(data[hdr:])))
    refresh_hdr_crc(data)
write_from('custom-none-full.rbl', 'parser-fuzz-header-fields-seed-0', lambda d: d.__setitem__(0, ord('X')))
write_from('custom-none-full.rbl', 'parser-fuzz-size-fields-seed-0', lambda d: (write_u32(d, PKG_SIZE_OFF, len(d) + 1024), refresh_hdr_crc(d)))
write_from('custom-none-full.rbl', 'parser-fuzz-product-version-nul-seed-0', lambda d: (d.__setitem__(slice(FW_VER_OFF, FW_VER_OFF + 24), b'v1\0evil'.ljust(24, b'\0')), d.__setitem__(slice(PROD_OFF, PROD_OFF + 24), b'host-product\0evil'.ljust(24, b'\0')), refresh_hdr_crc(d)))
write_from('custom-hpatch-host-full-diff.rbl', 'parser-fuzz-hpatch-body-varint-seed-0', mutate_hpatch_body_varint)
write_from('custom-gzip.rbl', 'parser-fuzz-gzip-trailer-seed-0', lambda d: update_body(d, bytes(d[hdr:-8])))
write_from('custom-quicklz-release.rbl', 'codec-stub-quicklz-fastlz-block-header-rejected', lambda d: (d.__setitem__(slice(hdr, hdr + 4), b'\xff\xff\xff\xff'), write_u32(d, PKG_CRC_OFF, crc32(bytes(d[hdr:]))), refresh_hdr_crc(d)))
PYNAMEDFUZZ
for fuzz_name in \
  parser-fuzz-header-fields-seed-0 \
  parser-fuzz-size-fields-seed-0 \
  parser-fuzz-product-version-nul-seed-0 \
  parser-fuzz-hpatch-body-varint-seed-0 \
  parser-fuzz-gzip-trailer-seed-0; do
  log_file="$log_dir/$fuzz_name.log"
  case_count=$((case_count + 1))
  runner="$runner_custom"
  case "$fuzz_name" in
    parser-fuzz-product-version-nul-seed-0)
      if ! "$runner_custom" --case "$fuzz_name"         --package "$fixture_dir/$fuzz_name.rbl"         --old-app "$fixture_dir/old_app.bin"         --new-app "$fixture_dir/new_app.bin"         --expect-receive 1 --expect-first-success 0 --expect-success 0         --expect-jump 0 --expect-sign 0 --expect-app old > "$log_file" 2>&1; then
        fail_case "$fuzz_name" "$log_file"
      fi
      pass_case "$fuzz_name" "$log_file" "embedded-NUL product field is rejected by release policy"
      continue
      ;;
    parser-fuzz-hpatch-body-varint-seed-0)
      if ! "$runner_hpatch_host" --case "$fuzz_name"         --package "$fixture_dir/$fuzz_name.rbl"         --old-app "$fixture_dir/old_app.bin"         --new-app "$fixture_dir/hpatch_new_app.bin"         --expect-receive 1 --expect-first-success 0 --expect-success 0         --expect-jump 0 --expect-sign 0 --expect-app old --chunk 257 > "$log_file" 2>&1; then
        fail_case "$fuzz_name" "$log_file"
      fi
      pass_case "$fuzz_name" "$log_file" "malformed HPatchLite body is rejected during release"
      continue
      ;;
    parser-fuzz-gzip-trailer-seed-0)
      if ! "$runner_custom" --case "$fuzz_name"         --package "$fixture_dir/$fuzz_name.rbl"         --old-app "$fixture_dir/old_app.bin"         --new-app "$fixture_dir/new_app.bin"         --expect-receive 1 --expect-first-success 0 --expect-success 0         --expect-jump 0 --expect-sign 0 --expect-app old --chunk 257 > "$log_file" 2>&1; then
        fail_case "$fuzz_name" "$log_file"
      fi
      pass_case "$fuzz_name" "$log_file" "malformed gzip trailer is rejected during release"
      continue
      ;;
  esac
  if "$runner" --inspect --package "$fixture_dir/$fuzz_name.rbl" > "$log_file" 2>&1; then
    fail_case "$fuzz_name" "$log_file"
  fi
  pass_case "$fuzz_name" "$log_file" "named parser fuzz package rejected safely"
done

stub_case=codec-stub-quicklz-fastlz-block-header-rejected
log_file="$log_dir/$stub_case.log"
case_count=$((case_count + 1))
if ! "$runner_quickfast" --case "$stub_case" \
    --package "$fixture_dir/$stub_case.rbl" \
    --old-app "$fixture_dir/old_app.bin" \
    --new-app "$fixture_dir/quickfast_app.bin" \
    --expect-receive 1 --expect-first-success 0 --expect-success 0 \
    --expect-jump 0 --expect-sign 0 --expect-app old --chunk 257 > "$log_file" 2>&1; then
  fail_case "$stub_case" "$log_file"
fi
pass_case "$stub_case" "$log_file" "compile-matrix QuickLZ/FastLZ placeholder rejects malformed package without claiming parser-fuzz coverage"

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


run_parser_roundtrip() {
  local case_name=$1 pkg_name=$2 log_file py_log
  log_file="$log_dir/$case_name.log"
  py_log="$log_dir/$case_name.python.log"
  case_count=$((case_count + 1))
  "$runner_custom" --inspect --package "$fixture_dir/$pkg_name" > "$log_file" 2>&1 || fail_case "$case_name" "$log_file"
  if ! "$python_bin" - "$fixture_dir/$pkg_name" "$log_file" > "$py_log" 2>&1 <<'PYROUND'
from pathlib import Path
import importlib.util, json, sys
spec = importlib.util.spec_from_file_location('package_tool_web', Path('docs/package-tool/package_tool_web.py'))
ptw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ptw)
header = ptw.parse_rbl_header(Path(sys.argv[1]).read_bytes())
lines = [line for line in Path(sys.argv[2]).read_text(encoding='utf-8').splitlines() if line.startswith('{')]
if not lines:
    raise SystemExit('C inspect output did not contain JSON')
c_header = json.loads(lines[-1])
checks = {
    'algo': header['algo'],
    'algo2': header['algo2'],
    'part_name': header['part'],
    'fw_ver': header['version'],
    'prod_code': header['product'],
    'pkg_crc': header['pkg_crc'],
    'raw_crc': header['raw_crc'],
    'raw_size': header['raw_size'],
    'pkg_size': header['pkg_size'],
    'hdr_crc': header['hdr_crc'],
}
for key, expected in checks.items():
    if c_header.get(key) != expected:
        raise SystemExit(f'{key}: C={c_header.get(key)!r} Python={expected!r}')
PYROUND
  then
    cat "$py_log" >> "$log_file"
    fail_case "$case_name" "$log_file"
  fi
  cat "$py_log" >> "$log_file"
  pass_case "$case_name" "$log_file" "C parser and Python package tool header round-trip match"
}

for item in \
  "parser-c-python-roundtrip-none:custom-none-full.rbl" \
  "parser-c-python-roundtrip-gzip:custom-gzip.rbl" \
  "parser-c-python-roundtrip-aes-gzip:custom-aes-gzip-real.rbl" \
  "parser-c-python-roundtrip-hpatch:custom-hpatch-host-full-diff.rbl"; do
  run_parser_roundtrip "${item%%:*}" "${item#*:}"
done
if [ "$hpatch_production_available" = "1" ]; then
  run_parser_roundtrip parser-c-python-roundtrip-hpatch-multi custom-hpatch-production-multi-cover.rbl
else
  skip_case parser-c-python-roundtrip-hpatch-multi "production HPatchLite multi-cover fixture is missing in dependency bootstrap mode"
fi

for item in \
  "stream-aes-gzip-chunk-15:custom-aes-gzip-real.rbl:aes_new_app.bin:15" \
  "stream-aes-gzip-chunk-16:custom-aes-gzip-real.rbl:aes_new_app.bin:16" \
  "stream-aes-gzip-chunk-17:custom-aes-gzip-real.rbl:aes_new_app.bin:17" \
  "stream-aes-gzip-chunk-31:custom-aes-gzip-real.rbl:aes_new_app.bin:31" \
  "stream-aes-gzip-chunk-32:custom-aes-gzip-real.rbl:aes_new_app.bin:32" \
  "stream-hpatch-cross-chunk-15:custom-hpatch-host-full-diff.rbl:hpatch_new_app.bin:15:hpatch" \
  "stream-hpatch-cross-chunk-17:custom-hpatch-host-full-diff.rbl:hpatch_new_app.bin:17:hpatch"; do
  case_name=${item%%:*}
  rest=${item#*:}
  pkg=${rest%%:*}
  rest=${rest#*:}
  new_img=${rest%%:*}
  rest=${rest#*:}
  chunk=${rest%%:*}
  runner="$runner_custom"
  if [ "${rest#*:}" = "hpatch" ]; then
    runner="$runner_hpatch_host"
  fi
  run_release "$runner" "$case_name" "$fixture_dir/$pkg" "$fixture_dir/old_app.bin" "$fixture_dir/$new_img" success --chunk "$chunk"
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

for fault_sequence_case in fault-sequence-erase-write-sign-success fault-sequence-read-write-signread-success fault-sequence-app-write-retry-success fault-sequence-sign-retry-success; do
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
for update_case in callback-progress-monotonic callback-progress-final-100-on-success callback-error-code-propagated error-code-update-in-progress error-code-update-not-started update-mgr-wait-timeout-app-valid-clears-reason update-mgr-wait-timeout-recover-clears-reason update-mgr-idle-timeout-app-valid-clears-reason update-mgr-idle-timeout-recover-clears-reason; do
  log_file="$log_dir/$update_case.log"
  case_count=$((case_count + 1))
  "$runner_custom" --mode update-mgr --case "$update_case" > "$log_file" 2>&1 || fail_case "$update_case" "$log_file"
  pass_case "$update_case" "$log_file" "progress/error-code contract"
done


for stale_case in \
  mixed-backend-stale-fal-download-with-valid-sign-rejected \
  sign-same-size-different-raw-crc-rejected \
  sign-same-size-different-product-rejected \
  sign-same-size-different-version-current-policy \
  sign-valid-marker-stale-download-body-rejected \
  sign-copied-from-old-package-to-new-package-rejected; do
  runner="$runner_custom"
  case "$stale_case" in
    mixed-*) runner="$runner_mixed" ;;
  esac
  log_file="$log_dir/$stale_case.log"
  case_count=$((case_count + 1))
  "$runner" --mode stale-sign --case "$stale_case" --fixture-dir "$fixture_dir" > "$log_file" 2>&1 || fail_case "$stale_case" "$log_file"
  pass_case "$stale_case" "$log_file" "copied release sign cannot skip package validation"
done

for item in \
  "fs-fault-sequence-read-write-signread-success:$runner_fs" \
  "fs-fault-sequence-app-write-retry-current-policy:$runner_fs" \
  "gzip-fault-sequence-app-write-retry-success:$runner_custom" \
  "aes-gzip-fault-sequence-read-write-signread-success:$runner_custom" \
  "hpatch-fault-sequence-app-write-retry-success:$runner_hpatch_host"; do
  fault_case=${item%%:*}
  runner=${item#*:}
  log_file="$log_dir/$fault_case.log"
  case_count=$((case_count + 1))
  "$runner" --mode fault-sequence --case "$fault_case" --fixture-dir "$fixture_dir" > "$log_file" 2>&1 || fail_case "$fault_case" "$log_file"
  pass_case "$fault_case" "$log_file" "named deterministic fault sequence"
done

fault_case=hpatch-fault-sequence-swap-write-retry-success
log_file="$log_dir/$fault_case.log"
if [ -x "$runner_hpatch_storage" ]; then
  case_count=$((case_count + 1))
  "$runner_hpatch_storage" --mode fault-sequence --case "$fault_case" --fixture-dir "$fixture_dir" > "$log_file" 2>&1 || fail_case "$fault_case" "$log_file"
  pass_case "$fault_case" "$log_file" "named deterministic HPatch storage-swap fault sequence"
else
  skip_case "$fault_case" "storage-swap HPatchLite runner is missing"
fi

for item in \
  "fal-update-helper-abort-clears-session:$runner_fal" \
  "fs-update-helper-abort-clears-session:$runner_fs" \
  "mixed-update-helper-abort-clears-session:$runner_mixed" \
  "fal-update-helper-ready-close-fail-retries-before-ready:$runner_fal" \
  "fs-update-helper-close-fail-on-reject-propagated:$runner_fs" \
  "mixed-update-helper-write-fail-then-restart:$runner_mixed"; do
  update_case=${item%%:*}
  runner=${item#*:}
  log_file="$log_dir/$update_case.log"
  case_count=$((case_count + 1))
  "$runner" --mode update-mgr --case "$update_case" > "$log_file" 2>&1 || fail_case "$update_case" "$log_file"
  pass_case "$update_case" "$log_file" "backend-specific update-helper state contract"
done

for item in \
  "resource-repeat-50-fail-then-success-no-handle-growth:$runner_custom" \
  "resource-repeat-50-sign-write-fail-no-sign-leak:$runner_custom" \
  "resource-repeat-50-fs-open-fail-no-fd-growth:$runner_fs"; do
  resource_case=${item%%:*}
  runner=${item#*:}
  log_file="$log_dir/$resource_case.log"
  case_count=$((case_count + 1))
  "$runner" --mode repeat-sequence --case "$resource_case" --fixture-dir "$fixture_dir" > "$log_file" 2>&1 || fail_case "$resource_case" "$log_file"
  pass_case "$resource_case" "$log_file" "50 in-process repeated failure/recovery checks complete with resource trackers idle"
done

resource_case=resource-repeat-50-hpatch-malloc-fail-no-swap-leftover
log_file="$log_dir/$resource_case.log"
if [ -x "$runner_hpatch_storage" ]; then
  case_count=$((case_count + 1))
  "$runner_hpatch_storage" --mode repeat-sequence --case "$resource_case" --fixture-dir "$fixture_dir" > "$log_file" 2>&1 || fail_case "$resource_case" "$log_file"
  pass_case "$resource_case" "$log_file" "50 in-process HPatch storage-swap malloc-failure checks complete with resource trackers idle"
else
  skip_case "$resource_case" "storage-swap HPatchLite runner is missing"
fi

case_name=resource-repeat-100-upgrades-no-state-growth
log_file="$log_dir/$case_name.log"
case_count=$((case_count + 1))
"$runner_custom" --mode repeat-sequence --case repeat-upgrade-100-none-no-state-growth --fixture-dir "$fixture_dir" > "$log_file" 2>&1 || fail_case "$case_name" "$log_file"
pass_case "$case_name" "$log_file" "100 in-process repeated releases keep storage/session state reusable"

executed_count=$((case_count - skip_count))
printf '\nPassed %d/%d executed QBoot host extended coverage checks; skipped %d optional checks; enumerated %d total checks.\n' \
  "$pass_count" "$executed_count" "$skip_count" "$case_count" >> "$summary"
printf 'Passed %d/%d executed QBoot host extended coverage checks; skipped %d optional checks; enumerated %d total checks.\n' \
  "$pass_count" "$executed_count" "$skip_count" "$case_count"
