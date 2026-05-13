#!/usr/bin/env bash
set -euo pipefail

host_root="${QBOOT_HOST_OUT_DIR:-_ci/host-sim}"
runtime_dir="$host_root/runtime-codecs"
pkg_root="${QBOOT_HOST_CODEC_PACKAGE_DIR:-$runtime_dir/packages}"
build_out="$runtime_dir/bin"
fixture_dir="$runtime_dir/fixtures"
log_dir="$runtime_dir/logs"
summary="$runtime_dir/runtime_codec_summary.md"
source_manifest="$runtime_dir/source_manifest.txt"
pre_cflags_file="$runtime_dir/pre_cflags.args"
extra_sources_file="$runtime_dir/extra_sources.args"
compat_dir="$runtime_dir/include"

mkdir -p "$runtime_dir" "$build_out" "$fixture_dir" "$log_dir" "$compat_dir"

# This job validates QBoot against external runtime codec sources. Keep
# self-checks for the CI harness, artifact inventory, or generated logs out of
# this path unless they directly protect QBoot release behavior.
run_logged() {
  local log_file=$1
  shift
  if ! "$@" > "$log_file" 2>&1; then
    printf 'QBOOT_HOST_RUNTIME_CODEC_FAIL %s\n' "$log_file" >&2
    cat "$log_file" >&2
    exit 1
  fi
}

run_logged "$log_dir/prepare-packages.log" env QBOOT_HOST_CODEC_PACKAGE_DIR="$pkg_root" bash .github/ci/qboot/prepare-host-codec-packages.sh

select_single_path() {
  local description=$1 selected=$2 count

  if [ -z "$selected" ]; then
    echo "missing $description" >&2
    exit 1
  fi

  count=$(printf '%s\n' "$selected" | wc -l | tr -d ' ')
  if [ "$count" -ne 1 ]; then
    echo "ambiguous $description" >&2
    printf '%s\n' "$selected" >&2
    exit 1
  fi
  printf '%s\n' "$selected"
}

find_single_file() {
  local package=$1 file_name=$2 selected
  selected=$(find "$pkg_root/$package" -type f -name "$file_name" | sort -u)
  select_single_path "$file_name in $pkg_root/$package" "$selected"
}

find_preferred_header() {
  local package=$1 header=$2 candidate
  shift 2

  for candidate in "$@"; do
    if [ -f "$pkg_root/$package/$candidate" ]; then
      printf '%s\n' "$pkg_root/$package/$candidate"
      return 0
    fi
  done
  find_single_file "$package" "$header"
}

find_header_path() {
  local package=$1 header=$2

  case "$package:$header" in
    crclib:crc32.h)
      find_preferred_header crclib crc32.h crc32.h inc/crc32.h src/crc32.h
      ;;
    tinycrypt:tinycrypt.h)
      find_preferred_header tinycrypt tinycrypt.h tinycrypt.h inc/tinycrypt.h src/tinycrypt.h
      ;;
    zlib:zlib.h)
      find_preferred_header zlib zlib.h src/zlib.h zlib.h
      ;;
    quicklz:quicklz.h)
      find_preferred_header quicklz quicklz.h quicklz.h inc/quicklz.h src/quicklz.h
      ;;
    fastlz:fastlz.h)
      find_preferred_header fastlz fastlz.h fastlz.h inc/fastlz.h src/fastlz.h
      ;;
    hpatchlite:hpatch_impl.h)
      find_preferred_header hpatchlite hpatch_impl.h hpatch_impl.h inc/hpatch_impl.h src/hpatch_impl.h
      ;;
    *)
      find_single_file "$package" "$header"
      ;;
  esac
}

find_header_dir() {
  local package=$1 header=$2 file_path

  if ! file_path=$(find_header_path "$package" "$header"); then
    exit 1
  fi
  dirname "$file_path"
}

find_header_dirs() {
  local package=$1 dirs
  dirs=$(find "$pkg_root/$package" -type f -name '*.h' -printf '%h\n' | sort -u)
  if [ -z "$dirs" ]; then
    echo "missing headers in $pkg_root/$package" >&2
    exit 1
  fi
  printf '%s\n' "$dirs"
}

find_sources_with_symbol() {
  local package=$1 symbol=$2 result
  result=$(grep -Rsl --include='*.c' "$symbol" "$pkg_root/$package" | \
    while IFS= read -r src; do
      case "$src" in
        */test/*|*/tests/*|*/example/*|*/examples/*|*/sample/*|*/samples/*|*/demo/*|*/demos/*|*sample*.c|*demo*.c)
          continue
          ;;
      esac
      printf '%s\n' "$src"
    done || true)
  if [ -z "$result" ]; then
    echo "missing C source with $symbol in $pkg_root/$package" >&2
    return 1
  fi
  printf '%s\n' "$result"
}

source_already_selected() {
  local wanted=$1 selected

  for selected in "${source_list[@]}"; do
    if [ "$selected" = "$wanted" ]; then
      return 0
    fi
  done
  return 1
}

append_source_path() {
  local src=$1

  if ! source_already_selected "$src"; then
    printf 'source %s\n' "$src" >> "$source_manifest"
    source_list+=("$src")
  fi
}

select_required_source() {
  local package=$1 symbol=$2 selected count

  if ! selected=$(find_sources_with_symbol "$package" "$symbol") || [ -z "$selected" ]; then
    echo "failed to select source for $package symbol $symbol" >&2
    exit 1
  fi

  count=$(printf '%s\n' "$selected" | wc -l | tr -d ' ')
  if [ "$count" -ne 1 ]; then
    echo "ambiguous sources for $package symbol $symbol" >&2
    printf '%s\n' "$selected" >&2
    exit 1
  fi
  printf '%s\n' "$selected"
}

select_hpatch_lite_core_source() {
  local selected count

  selected=$(find "$pkg_root/hpatchlite" -type f \
    -path '*/libHDiffPatch/HPatchLite/hpatch_lite.c' -print)
  if [ -z "$selected" ]; then
    selected=$(find "$pkg_root/hpatchlite" -type f -name hpatch_lite.c \
      -exec grep -l 'hpatch_lite_patch' {} + 2>/dev/null || true)
  fi
  if [ -z "$selected" ]; then
    echo "missing HPatchLite core source hpatch_lite.c in $pkg_root/hpatchlite" >&2
    exit 1
  fi

  count=$(printf '%s\n' "$selected" | wc -l | tr -d ' ')
  if [ "$count" -ne 1 ]; then
    echo "ambiguous HPatchLite core sources for hpatch_lite_patch" >&2
    printf '%s\n' "$selected" >&2
    exit 1
  fi
  printf '%s\n' "$selected"
}

append_optional_sources() {
  local selected=$1 src

  while IFS= read -r src; do
    [ -n "$src" ] || continue
    append_source_path "$src"
  done <<EOF_SOURCES
$selected
EOF_SOURCES
}

write_runtime_codec_adapter() {
  local crc_source=$1 tiny_source=$2 adapter_source=$3 crc_header=$4 tiny_header=$5

  crc_source=$(realpath "$crc_source")
  tiny_source=$(realpath "$tiny_source")
  crc_header=$(realpath "$crc_header")
  tiny_header=$(realpath "$tiny_header")
  cat > "$adapter_source" <<EOF_ADAPTER
/**
 * @file qboot_host_runtime_codec_adapter.c
 * @brief Runtime-only adapter from QBoot host ABI to staged codec packages.
 *
 * This generated translation unit intentionally includes the external package
 * headers and implementation files with symbol renaming, then exports the
 * symbols expected by QBoot. It is not a host codec stub: release tests still
 * execute the staged crclib and TinyCrypt implementation code.
 */
#include <rtthread.h>
#include <stdint.h>

#define CRCLIB_USING_CRC32
#define CRC32_USING_CONST_TABLE
#define crc32_cal qboot_ext_crc32_cal
#define crc32_cyc_cal qboot_ext_crc32_cyc_cal
#include "$crc_header"
#define QBOOT_HOST_CODEC_COMPAT_CRC32_H
#include "$crc_source"
#undef QBOOT_HOST_CODEC_COMPAT_CRC32_H
#undef crc32_cal
#undef crc32_cyc_cal
#undef CRC32_USING_CONST_TABLE
#undef CRCLIB_USING_CRC32

#define TINY_CRYPT_AES
#define tiny_aes_setkey_dec qboot_ext_tiny_aes_setkey_dec
#define tiny_aes_crypt_cbc qboot_ext_tiny_aes_crypt_cbc
#include "$tiny_header"
#define QBOOT_HOST_CODEC_COMPAT_TINYCRYPT_H
#include "$tiny_source"
#undef QBOOT_HOST_CODEC_COMPAT_TINYCRYPT_H
#undef tiny_aes_setkey_dec
#undef tiny_aes_crypt_cbc
#undef TINY_CRYPT_AES

/**
 * @brief Calculate a one-shot CRC32 value through the staged crclib source.
 *
 * @param buf Data buffer.
 * @param len Data length in bytes.
 * @return Calculated CRC32 value.
 */
rt_uint32_t crc32_cal(rt_uint8_t *buf, rt_uint32_t len)
{
    return (rt_uint32_t)qboot_ext_crc32_cal((u8 *)buf, (u32)len);
}

/**
 * @brief Continue a CRC32 calculation through the staged crclib source.
 *
 * @param crc Current CRC32 accumulator.
 * @param buf Data buffer.
 * @param len Data length in bytes.
 * @return Updated CRC32 accumulator.
 */
rt_uint32_t crc32_cyc_cal(rt_uint32_t crc, rt_uint8_t *buf, rt_uint32_t len)
{
    return (rt_uint32_t)qboot_ext_crc32_cyc_cal((u32)crc, (u8 *)buf, (u32)len);
}

/**
 * @brief Initialize TinyCrypt AES decryption context.
 *
 * @param ctx AES context from the staged TinyCrypt package.
 * @param key AES key buffer.
 * @param keysize AES key size in bits.
 */
void tiny_aes_setkey_dec(tiny_aes_context *ctx, rt_uint8_t *key, int keysize)
{
    qboot_ext_tiny_aes_setkey_dec(ctx, key, keysize);
}

/**
 * @brief Run TinyCrypt AES-CBC encryption or decryption.
 *
 * @param ctx AES context from the staged TinyCrypt package.
 * @param mode AES mode, normally AES_DECRYPT for QBoot release.
 * @param length Input length in bytes.
 * @param iv 16-byte IV buffer, updated by TinyCrypt.
 * @param input Input buffer.
 * @param output Output buffer.
 */
void tiny_aes_crypt_cbc(tiny_aes_context *ctx,
                        int mode,
                        int length,
                        rt_uint8_t iv[16],
                        rt_uint8_t *input,
                        rt_uint8_t *output)
{
    qboot_ext_tiny_aes_crypt_cbc(ctx, mode, length, iv, input, output);
}
EOF_ADAPTER
}
find_zlib_sources() {
  local zlib_dir="$pkg_root/zlib" src_dir src found
  if [ -d "$zlib_dir/src" ]; then
    src_dir="$zlib_dir/src"
  else
    src_dir="$zlib_dir"
  fi
  for src in adler32.c crc32.c inflate.c inffast.c inftrees.c zutil.c; do
    if [ -f "$src_dir/$src" ]; then
      printf '%s\n' "$src_dir/$src"
    else
      found=$(find "$src_dir" -type f -name "$src" | sort -u)
      if [ -z "$found" ]; then
        echo "missing required zlib source $src in $src_dir (pkg_root=$pkg_root, QBOOT_HOST_NO_SYSTEM_ZLIB=1)" >&2
        exit 1
      fi
      select_single_path "$src in $src_dir" "$found"
    fi
  done
}


select_hpatch_tuz_source() {
  local selected count

  selected=$(find "$pkg_root/hpatchlite" -type f -name tuz_dec.c \
    ! -path '*/test/*' ! -path '*/tests/*' \
    ! -path '*/example/*' ! -path '*/examples/*' \
    ! -path '*/sample/*' ! -path '*/samples/*' \
    ! -path '*/demo/*' ! -path '*/demos/*' | sort -u)
  if [ -z "$selected" ]; then
    selected=$(find "$pkg_root/hpatchlite" -type f -path '*/decompress/*' \
      -name '*tuz*.c' \
      ! -path '*/test/*' ! -path '*/tests/*' \
      ! -path '*/example/*' ! -path '*/examples/*' \
      ! -path '*/sample/*' ! -path '*/samples/*' \
      ! -path '*/demo/*' ! -path '*/demos/*' \
      ! -name '*demo*.c' ! -name '*sample*.c' | sort -u)
  fi
  if [ -z "$selected" ]; then
    echo "missing HPatchLite TUZ decompressor source in $pkg_root/hpatchlite" >&2
    exit 1
  fi

  count=$(printf '%s\n' "$selected" | wc -l | tr -d ' ')
  if [ "$count" -ne 1 ]; then
    echo "ambiguous HPatchLite TUZ decompressor sources" >&2
    printf '%s\n' "$selected" >&2
    exit 1
  fi
  printf '%s\n' "$selected"
}


write_compat_headers() {
  local crc_header tiny_header

  crc_header=$(find_header_path crclib crc32.h)
  crc_header=$(realpath "$crc_header")
  cat > "$compat_dir/crc32.h" <<EOF_CRC32
#ifndef QBOOT_HOST_CODEC_COMPAT_CRC32_H
#define QBOOT_HOST_CODEC_COMPAT_CRC32_H
#include <rtthread.h>

#if !defined(crc32_cal)
rt_uint32_t crc32_cal(rt_uint8_t *buf, rt_uint32_t len);
#endif /* !defined(crc32_cal) */
#if !defined(crc32_cyc_cal)
rt_uint32_t crc32_cyc_cal(rt_uint32_t crc, rt_uint8_t *buf, rt_uint32_t len);
#endif /* !defined(crc32_cyc_cal) */
#endif /* QBOOT_HOST_CODEC_COMPAT_CRC32_H */
EOF_CRC32

  tiny_header=$(grep -Rsl --include='*.h' 'tiny_aes_context' "$pkg_root/tinycrypt" | sort -u || true)
  tiny_header=$(select_single_path "header with tiny_aes_context in $pkg_root/tinycrypt" "$tiny_header")
  tiny_header=$(realpath "$tiny_header")
  cat > "$compat_dir/tinycrypt.h" <<EOF_TINYCRYPT
#ifndef QBOOT_HOST_CODEC_COMPAT_TINYCRYPT_H
#define QBOOT_HOST_CODEC_COMPAT_TINYCRYPT_H
#include <rtthread.h>
#include "$tiny_header"
#ifndef AES_DECRYPT
#define AES_DECRYPT 0
#endif /* AES_DECRYPT */
#endif /* QBOOT_HOST_CODEC_COMPAT_TINYCRYPT_H */
EOF_TINYCRYPT
}

write_compat_headers

if ! crc_adapter_header=$(find_header_path crclib crc32.h); then
  exit 1
fi
if ! tiny_adapter_header=$(find_header_path tinycrypt tinycrypt.h); then
  exit 1
fi

if ! hpatch_header_dir=$(find_header_dir hpatchlite hpatch_impl.h); then
  exit 1
fi
if ! hpatch_header_dirs=$(find_header_dirs hpatchlite); then
  exit 1
fi

write_arg_file() {
  local file_path=$1 arg
  shift

  : > "$file_path"
  for arg in "$@"; do
    printf '%s\n' "$arg" >> "$file_path"
  done
}

append_include_dir() {
  local dir=$1

  [ -n "$dir" ] || return 0
  include_flags+=("-I$dir")
}

append_include_dirs() {
  local dirs=$1 dir

  while IFS= read -r dir; do
    append_include_dir "$dir"
  done <<EOF_INCLUDE_DIRS
$dirs
EOF_INCLUDE_DIRS
}

include_flags=("-I$compat_dir")
if ! header_dir=$(find_header_dir crclib crc32.h); then
  exit 1
fi
append_include_dir "$header_dir"
if ! header_dir=$(find_header_dir tinycrypt tinycrypt.h); then
  exit 1
fi
append_include_dir "$header_dir"
if ! header_dir=$(find_header_dir zlib zlib.h); then
  exit 1
fi
append_include_dir "$header_dir"
if ! header_dir=$(find_header_dir quicklz quicklz.h); then
  exit 1
fi
append_include_dir "$header_dir"
if ! header_dir=$(find_header_dir fastlz fastlz.h); then
  exit 1
fi
append_include_dir "$header_dir"
append_include_dirs "$hpatch_header_dirs"

source_list=()
runtime_adapter="$runtime_dir/qboot_host_runtime_codec_adapter.c"
printf '# QBoot runtime codec source manifest\n\n' > "$source_manifest"
if ! crc_source=$(select_required_source crclib crc32_cal); then
  exit 1
fi
if ! crc_cyc_source=$(select_required_source crclib crc32_cyc_cal); then
  exit 1
fi
if [ "$crc_source" != "$crc_cyc_source" ]; then
  echo "crclib CRC32 symbols are split across unsupported sources: $crc_source $crc_cyc_source" >&2
  exit 1
fi
if ! tiny_source=$(select_required_source tinycrypt tiny_aes_setkey_dec); then
  exit 1
fi
if ! tiny_cbc_source=$(select_required_source tinycrypt tiny_aes_crypt_cbc); then
  exit 1
fi
if [ "$tiny_source" != "$tiny_cbc_source" ]; then
  echo "TinyCrypt AES symbols are split across unsupported sources: $tiny_source $tiny_cbc_source" >&2
  exit 1
fi
write_runtime_codec_adapter "$crc_source" "$tiny_source" "$runtime_adapter" "$crc_adapter_header" "$tiny_adapter_header"
printf 'adapter %s\n' "$runtime_adapter" >> "$source_manifest"
printf 'external crclib %s\n' "$crc_source" >> "$source_manifest"
printf 'external tinycrypt %s\n' "$tiny_source" >> "$source_manifest"
append_source_path "$runtime_adapter"
if ! zlib_sources=$(find_zlib_sources); then
  exit 1
fi
append_optional_sources "$zlib_sources"
if ! quicklz_source=$(select_required_source quicklz qlz_decompress); then
  exit 1
fi
append_source_path "$quicklz_source"
if ! fastlz_source=$(select_required_source fastlz fastlz_decompress); then
  exit 1
fi
append_source_path "$fastlz_source"
if ! hpatch_source=$(select_required_source hpatchlite hpi_patch); then
  exit 1
fi
if ! hpatch_lite_source=$(select_hpatch_lite_core_source); then
  exit 1
fi
if ! hpatch_tuz_source=$(select_hpatch_tuz_source); then
  exit 1
fi
printf 'external hpatchlite %s\n' "$hpatch_source" >> "$source_manifest"
printf 'external hpatchlite-core %s\n' "$hpatch_lite_source" >> "$source_manifest"
printf 'external hpatchlite-tuz %s\n' "$hpatch_tuz_source" >> "$source_manifest"
if [ "$hpatch_lite_source" != "$hpatch_source" ]; then
  append_source_path "$hpatch_lite_source"
fi
append_source_path "$hpatch_tuz_source"
write_arg_file "$pre_cflags_file" "${include_flags[@]}"
write_arg_file "$extra_sources_file" "${source_list[@]}"

# Runtime codec integration must link the external package implementations.
# The generated adapter only maps QBoot's CRC/AES ABI to staged crclib and
# TinyCrypt sources by including their implementation files with symbol
# renaming. It does not provide fake codec behavior, and the source manifest is
# uploaded with the runtime codec artifact so CI shows which external package
# files backed each runtime symbol.
run_logged "$log_dir/build-codec-runtime.log" env \
  QBOOT_HOST_OUT_DIR="$build_out" \
  QBOOT_HOST_BACKENDS="custom-codec-runtime custom-codec-runtime-hpatch" \
  QBOOT_HOST_PRE_CFLAGS_FILE="$pre_cflags_file" \
  QBOOT_HOST_EXTRA_CFLAGS="-Wno-implicit-fallthrough -D_CompressPlugin_tuz" \
  QBOOT_HOST_EXTRA_SOURCES_FILE="$extra_sources_file" \
  QBOOT_HOST_NO_SYSTEM_ZLIB=1 \
  QBOOT_HOST_HPATCHLITE_PACKAGE_DIR="$hpatch_header_dir" \
  QBOOT_HOST_HPATCHLITE_SOURCE="$hpatch_source" \
  bash .github/ci/qboot/build-host-sim.sh

runner="$build_out/qboot_host_runner_custom-codec-runtime"
runner_hpatch="$build_out/qboot_host_runner_custom-codec-runtime-hpatch"
for built_runner in "$runner" "$runner_hpatch"; do
  if [ ! -x "$built_runner" ]; then
    printf 'QBOOT_HOST_RUNTIME_CODEC_FAIL missing-runner %s\n' "$built_runner" >&2
    cat "$log_dir/build-codec-runtime.log" >&2
    exit 1
  fi
done

python_bin="${PYTHON:-python3}"
if ! QBOOT_RUNTIME_FIXTURE_DIR="$fixture_dir" "$python_bin" - <<'PY' > "$log_dir/generate-fixtures.log" 2>&1
from pathlib import Path
import importlib.util
import os
import struct
import zlib

root = Path(os.environ['QBOOT_RUNTIME_FIXTURE_DIR'])
root.mkdir(parents=True, exist_ok=True)
spec = importlib.util.spec_from_file_location('package_tool_web', 'docs/package-tool/package_tool_web.py')
ptw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ptw)

HEADER_SIZE = 96
PKG_CRC_OFF = 76
PKG_SIZE_OFF = 88
HDR_CRC_OFF = 92

def pattern(size, step, seed):
    return bytes(((seed + i * step + (i >> 3)) & 0xFF) for i in range(size))

def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF

def write_u32(data, off, value):
    data[off:off + 4] = struct.pack('<I', value)

def refresh_hdr_crc(data):
    write_u32(data, HDR_CRC_OFF, crc32(bytes(data[:HDR_CRC_OFF])))

def update_body(data, body):
    del data[HEADER_SIZE:]
    data.extend(body)
    write_u32(data, PKG_CRC_OFF, crc32(body))
    write_u32(data, PKG_SIZE_OFF, len(body))
    refresh_hdr_crc(data)

def mutate_from(base_path, name, edit):
    data = bytearray(base_path.read_bytes())
    edit(data)
    out = root / name
    out.write_bytes(data)
    return out

def truncate_body(data):
    update_body(data, bytes(data[HEADER_SIZE:-1]))

def block_size_overrun(data, label):
    body = bytearray(data[HEADER_SIZE:])
    if len(body) < 4:
        raise SystemExit(f'{label} fixture body too small')
    block_size = int.from_bytes(body[:4], 'big')
    body[:4] = (block_size + 1).to_bytes(4, 'big')
    update_body(data, bytes(body))

def block_size_wraparound(data, label):
    body = bytearray(data[HEADER_SIZE:])
    if len(body) < 4:
        raise SystemExit(f'{label} fixture body too small')
    body[:4] = (0xFFFFFFFF).to_bytes(4, 'big')
    update_body(data, bytes(body))

old_app = pattern(4096, 5, 0x11)
new_app = pattern(4096, 7, 0x33)
hpatch_new_app = pattern(4096, 9, 0x41)
quicklz_app = pattern(4096, 11, 0x55)
fastlz_app = pattern(4096, 13, 0x61)
quicklz_large_app = pattern(4096 + 257, 17, 0x29)
fastlz_large_app = pattern(4096 + 257, 23, 0x35)
plus_one_app = pattern(0x30001, 19, 0x72)

root.joinpath('old_app.bin').write_bytes(old_app)
root.joinpath('new_app.bin').write_bytes(new_app)
root.joinpath('hpatch_new_app.bin').write_bytes(hpatch_new_app)
root.joinpath('quicklz_app.bin').write_bytes(quicklz_app)
root.joinpath('fastlz_app.bin').write_bytes(fastlz_app)
root.joinpath('quicklz_large_app.bin').write_bytes(quicklz_large_app)
root.joinpath('fastlz_large_app.bin').write_bytes(fastlz_large_app)
root.joinpath('plus_one_app.bin').write_bytes(plus_one_app)

product = 'host-product'
root.joinpath('runtime-none.rbl').write_bytes(ptw.package_firmware_bytes(new_app, crypt='none', cmprs='none', algo2='crc', part='app', version='v-runtime-none', product=product, timestamp=1700010000))
root.joinpath('runtime-gzip-zlib.rbl').write_bytes(ptw.package_firmware_bytes(new_app, crypt='none', cmprs='gzip', algo2='crc', part='app', version='v-runtime-gzip', product=product, timestamp=1700010001))
root.joinpath('runtime-aes-tinycrypt.rbl').write_bytes(ptw.package_firmware_bytes(new_app, crypt='aes', cmprs='none', algo2='crc', part='app', version='v-runtime-aes', product=product, timestamp=1700010002))

for size in range(4096, 0x30000 + 1):
    aes_gzip_app = pattern(size, 17, 0x27)
    if len(ptw.gzip_compress(aes_gzip_app)) % 16 == 0:
        break
else:
    raise SystemExit('failed to find AES-compatible gzip fixture')
root.joinpath('aes_gzip_app.bin').write_bytes(aes_gzip_app)
root.joinpath('runtime-aes-gzip.rbl').write_bytes(ptw.package_firmware_bytes(aes_gzip_app, crypt='aes', cmprs='gzip', algo2='crc', part='app', version='v-runtime-aes-gzip', product=product, timestamp=1700010003))

runtime_quicklz = root / 'runtime-quicklz.rbl'
runtime_fastlz = root / 'runtime-fastlz.rbl'
runtime_quicklz.write_bytes(ptw.package_firmware_bytes(quicklz_app, crypt='none', cmprs='quicklz', algo2='crc', part='app', version='v-runtime-quicklz', product=product, timestamp=1700010004))
runtime_fastlz.write_bytes(ptw.package_firmware_bytes(fastlz_app, crypt='none', cmprs='fastlz', algo2='crc', part='app', version='v-runtime-fastlz', product=product, timestamp=1700010005))
(root / 'runtime-quicklz-split-block.rbl').write_bytes(ptw.package_firmware_bytes(quicklz_large_app, crypt='none', cmprs='quicklz', algo2='crc', part='app', version='v-runtime-quicklz-split', product=product, timestamp=1700010012))
(root / 'runtime-fastlz-split-block.rbl').write_bytes(ptw.package_firmware_bytes(fastlz_large_app, crypt='none', cmprs='fastlz', algo2='crc', part='app', version='v-runtime-fastlz-split', product=product, timestamp=1700010013))
(root / 'runtime-quicklz-output-too-large.rbl').write_bytes(ptw.package_firmware_bytes(plus_one_app, crypt='none', cmprs='quicklz', algo2='crc', part='app', version='v-runtime-quicklz-plus-one', product=product, timestamp=1700010014))
(root / 'runtime-fastlz-output-too-large.rbl').write_bytes(ptw.package_firmware_bytes(plus_one_app, crypt='none', cmprs='fastlz', algo2='crc', part='app', version='v-runtime-fastlz-plus-one', product=product, timestamp=1700010015))
mutate_from(runtime_quicklz, 'runtime-quicklz-truncated.rbl', truncate_body)
mutate_from(runtime_fastlz, 'runtime-fastlz-truncated.rbl', truncate_body)
mutate_from(runtime_quicklz, 'runtime-quicklz-block-size-overrun.rbl', lambda data: block_size_overrun(data, 'QuickLZ'))
mutate_from(runtime_fastlz, 'runtime-fastlz-block-size-overrun.rbl', lambda data: block_size_overrun(data, 'FastLZ'))
mutate_from(runtime_quicklz, 'runtime-quicklz-block-size-wraparound.rbl', lambda data: block_size_wraparound(data, 'QuickLZ'))
mutate_from(runtime_fastlz, 'runtime-fastlz-block-size-wraparound.rbl', lambda data: block_size_wraparound(data, 'FastLZ'))
root.joinpath('runtime-hpatchlite.rbl').write_bytes(ptw.package_hpatchlite_rbl_bytes(old_app, hpatch_new_app, crypt='none', algo2='crc', part='app', version='v-runtime-hpatch', product=product, timestamp=1700010006, patch_compress='none'))
root.joinpath('runtime-hpatchlite-tuz.rbl').write_bytes(ptw.package_hpatchlite_rbl_bytes(old_app, hpatch_new_app, crypt='none', algo2='crc', part='app', version='v-runtime-hpatch-tuz', product=product, timestamp=1700010007, patch_compress='tuz'))
PY
then
  printf 'QBOOT_HOST_RUNTIME_CODEC_FAIL %s\n' "$log_dir/generate-fixtures.log" >&2
  cat "$log_dir/generate-fixtures.log" >&2
  exit 1
fi

pass_count=0
case_count=0

run_release() {
  local case_name=$1 package=$2 new_app=$3 log_file
  log_file="$log_dir/$case_name.log"
  case_count=$((case_count + 1))
  if "$runner" --case "$case_name" \
      --package "$fixture_dir/$package" \
      --old-app "$fixture_dir/old_app.bin" \
      --new-app "$fixture_dir/$new_app" \
      --expect-receive 1 --expect-first-success 1 --expect-success 1 \
      --expect-jump 1 --expect-sign 1 --expect-app new \
      --chunk 257 > "$log_file" 2>&1; then
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_RUNTIME_CODEC_PASS %s\n' "$case_name"
  else
    printf 'QBOOT_HOST_RUNTIME_CODEC_FAIL %s\n' "$case_name" >&2
    cat "$log_file" >&2
    exit 1
  fi
}

run_reject() {
  local case_name=$1 package=$2 new_app=$3 log_file
  log_file="$log_dir/$case_name.log"
  case_count=$((case_count + 1))
  if "$runner" --case "$case_name" \
      --package "$fixture_dir/$package" \
      --old-app "$fixture_dir/old_app.bin" \
      --new-app "$fixture_dir/$new_app" \
      --expect-receive 1 --expect-first-success 0 --expect-success 0 \
      --expect-jump 0 --expect-sign 0 --expect-app old \
      --chunk 257 > "$log_file" 2>&1; then
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_RUNTIME_CODEC_PASS %s\n' "$case_name"
  else
    printf 'QBOOT_HOST_RUNTIME_CODEC_FAIL %s\n' "$case_name" >&2
    cat "$log_file" >&2
    exit 1
  fi
}

run_hpatch_release() {
  local case_name=$1 package=$2 new_app=$3 log_file
  log_file="$log_dir/$case_name.log"
  case_count=$((case_count + 1))
  if "$runner_hpatch" --case "$case_name" \
      --package "$fixture_dir/$package" \
      --old-app "$fixture_dir/old_app.bin" \
      --new-app "$fixture_dir/$new_app" \
      --expect-receive 1 --expect-first-success 1 --expect-success 1 \
      --expect-jump 1 --expect-sign 1 --expect-app new \
      --chunk 257 > "$log_file" 2>&1; then
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_RUNTIME_CODEC_PASS %s\n' "$case_name"
  else
    printf 'QBOOT_HOST_RUNTIME_CODEC_FAIL %s\n' "$case_name" >&2
    cat "$log_file" >&2
    exit 1
  fi
}

run_release runtime-none-baseline runtime-none.rbl new_app.bin
run_release runtime-zlib-gzip-release runtime-gzip-zlib.rbl new_app.bin
run_release runtime-tinycrypt-aes-release runtime-aes-tinycrypt.rbl new_app.bin
run_release runtime-tinycrypt-zlib-aes-gzip-release runtime-aes-gzip.rbl aes_gzip_app.bin
run_release runtime-quicklz-release runtime-quicklz.rbl quicklz_app.bin
run_release runtime-fastlz-release runtime-fastlz.rbl fastlz_app.bin
run_release runtime-quicklz-split-block-release runtime-quicklz-split-block.rbl quicklz_large_app.bin
run_release runtime-fastlz-split-block-release runtime-fastlz-split-block.rbl fastlz_large_app.bin
run_reject runtime-quicklz-output-too-large-rejected runtime-quicklz-output-too-large.rbl plus_one_app.bin
run_reject runtime-fastlz-output-too-large-rejected runtime-fastlz-output-too-large.rbl plus_one_app.bin
run_reject runtime-quicklz-truncated-rejected runtime-quicklz-truncated.rbl quicklz_app.bin
run_reject runtime-fastlz-truncated-rejected runtime-fastlz-truncated.rbl fastlz_app.bin
run_reject runtime-quicklz-block-size-overrun-rejected runtime-quicklz-block-size-overrun.rbl quicklz_app.bin
run_reject runtime-fastlz-block-size-overrun-rejected runtime-fastlz-block-size-overrun.rbl fastlz_app.bin
run_reject runtime-quicklz-block-size-wraparound-rejected runtime-quicklz-block-size-wraparound.rbl quicklz_app.bin
run_reject runtime-fastlz-block-size-wraparound-rejected runtime-fastlz-block-size-wraparound.rbl fastlz_app.bin
run_hpatch_release runtime-hpatchlite-release runtime-hpatchlite.rbl hpatch_new_app.bin
run_hpatch_release runtime-hpatchlite-tuz-release runtime-hpatchlite-tuz.rbl hpatch_new_app.bin

printf '# QBoot Host Runtime Codec Integration\n\nPassed %d/%d runtime codec release/reject cases.\n\n' "$pass_count" "$case_count" > "$summary"
printf -- '- External source packages: crclib, tinycrypt, zlib, quicklz, fastlz, hpatchlite-wrapper.\n' >> "$summary"
printf -- '- Codec symbols are resolved from the fetched package source trees; no host codec stubs are linked in this job.\n' >> "$summary"
printf -- '- HPatchLite runtime release cases link the staged wdfk-prog/hpatchlite-wrapper source.\n' >> "$summary"
printf -- '- Fixtures are generated by `docs/package-tool/package_tool_web.py` and then released through the C host runner.\n' >> "$summary"
printf -- '- Source manifest: `%s`.\n' "$source_manifest" >> "$summary"
