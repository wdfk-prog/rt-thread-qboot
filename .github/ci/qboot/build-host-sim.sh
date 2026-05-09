#!/usr/bin/env bash
set -euo pipefail

out_dir="_ci/host-sim"
mkdir -p "$out_dir"

cc=${CC:-gcc}
common_cflags=(
  -std=c11
  -Wall
  -Wextra
  -Werror
  -Wno-unused-function
  -Itests/host
  -Itests/host/stubs
  -Iinc
  -Ialgorithm
  -I.github/ci/qboot/packages/hpatchlite
)
common_ldflags=()

if [ "${QBOOT_HOST_SANITIZER:-0}" = "1" ]; then
  common_cflags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
  common_ldflags+=(-fsanitize=address,undefined)
fi
if [ "${QBOOT_HOST_ANALYZER:-0}" = "1" ]; then
  common_cflags+=(-fanalyzer)
fi
if [ -n "${QBOOT_HOST_EXTRA_CFLAGS:-}" ]; then
  # shellcheck disable=SC2206
  common_cflags+=(${QBOOT_HOST_EXTRA_CFLAGS})
fi
if [ -n "${QBOOT_HOST_EXTRA_LDFLAGS:-}" ]; then
  # shellcheck disable=SC2206
  common_ldflags+=(${QBOOT_HOST_EXTRA_LDFLAGS})
fi

common_cflags+=(-O0 -g)

common_sources=(
  tests/host/qboot_host_runner.c
  tests/host/qboot_host_flash.c
  tests/host/qboot_host_crc32.c
  tests/host/qboot_host_tinycrypt.c
  tests/host/qboot_host_hpatchlite.c
  src/qboot.c
  src/qboot_algo.c
  src/qboot_ops.c
  src/qboot_stream.c
  src/qboot_update.c
  algorithm/qboot_none.c
  algorithm/qboot_gzip.c
  algorithm/qboot_aes.c
)

backend_configs=()
default_backends="custom custom-smallbuf fal fs custom-helper custom-hpatch-production custom-hpatch-storage-swap"

add_backend() {
  local name=$1 cflags=$2 sources=$3 excludes=$4 use_package_override=$5

  backend_configs+=("$name|$cflags|$sources|$excludes|$use_package_override")
}

add_backend custom-helper \
  "-DQBOOT_HOST_BACKEND_CUSTOM" \
  "src/qboot_custom_ops.c" \
  "" \
  0
add_backend custom \
  "-DQBOOT_HOST_BACKEND_CUSTOM" \
  "src/qboot_custom_ops.c" \
  "" \
  1
add_backend custom-smallbuf \
  "-DQBOOT_HOST_BACKEND_CUSTOM -DQBOOT_BUF_SIZE=16" \
  "src/qboot_custom_ops.c" \
  "" \
  1
add_backend custom-no-gzip \
  "-DQBOOT_HOST_BACKEND_CUSTOM -DQBOOT_HOST_DISABLE_GZIP" \
  "src/qboot_custom_ops.c" \
  "" \
  1
add_backend custom-no-aes \
  "-DQBOOT_HOST_BACKEND_CUSTOM -DQBOOT_HOST_DISABLE_AES" \
  "src/qboot_custom_ops.c" \
  "" \
  1
add_backend custom-no-hpatch \
  "-DQBOOT_HOST_BACKEND_CUSTOM -DQBOOT_HOST_DISABLE_HPATCHLITE -Wno-unused-parameter" \
  "src/qboot_custom_ops.c" \
  "" \
  1
add_backend custom-gzip-only \
  "-DQBOOT_HOST_BACKEND_CUSTOM -DQBOOT_HOST_DISABLE_AES -DQBOOT_HOST_DISABLE_HPATCHLITE -Wno-unused-parameter" \
  "src/qboot_custom_ops.c" \
  "" \
  1
add_backend custom-aes-gzip-only \
  "-DQBOOT_HOST_BACKEND_CUSTOM -DQBOOT_HOST_DISABLE_HPATCHLITE -Wno-unused-parameter" \
  "src/qboot_custom_ops.c" \
  "" \
  1
add_backend custom-hpatch-only \
  "-DQBOOT_HOST_BACKEND_CUSTOM -DQBOOT_HOST_DISABLE_GZIP -DQBOOT_HOST_DISABLE_AES -Wno-unused-parameter" \
  "src/qboot_custom_ops.c" \
  "" \
  1
add_backend custom-hpatch-production \
  "-DQBOOT_HOST_BACKEND_CUSTOM -DQBOOT_HOST_DISABLE_GZIP -DQBOOT_HOST_DISABLE_AES -Wno-unused-parameter" \
  "src/qboot_custom_ops.c algorithm/qboot_hpatchlite.c .github/ci/qboot/packages/hpatchlite/hpatch_impl.c" \
  "tests/host/qboot_host_hpatchlite.c" \
  1
add_backend custom-hpatch-storage-swap \
  "-DQBOOT_HOST_BACKEND_CUSTOM -DQBOOT_HOST_HPATCH_STORAGE_SWAP -DQBOOT_HOST_DISABLE_GZIP -DQBOOT_HOST_DISABLE_AES -Wno-unused-parameter" \
  "src/qboot_custom_ops.c algorithm/qboot_hpatchlite.c .github/ci/qboot/packages/hpatchlite/hpatch_impl.c" \
  "tests/host/qboot_host_hpatchlite.c" \
  1
add_backend custom-minimal \
  "-DQBOOT_HOST_BACKEND_CUSTOM -DQBOOT_HOST_DISABLE_GZIP -DQBOOT_HOST_DISABLE_AES -DQBOOT_HOST_DISABLE_HPATCHLITE -Wno-unused-parameter" \
  "src/qboot_custom_ops.c" \
  "" \
  1
add_backend mixed-backend \
  "-DQBOOT_HOST_BACKEND_MIXED" \
  "src/qboot_custom_ops.c src/qboot_fal_ops.c src/qboot_fs_ops.c src/qboot_mux_ops.c tests/host/qboot_host_fal.c" \
  "" \
  1
add_backend none-disabled \
  "-DQBOOT_HOST_BACKEND_NONE" \
  "" \
  "" \
  1
add_backend fal \
  "-DQBOOT_HOST_BACKEND_FAL" \
  "src/qboot_fal_ops.c tests/host/qboot_host_fal.c" \
  "" \
  1
add_backend fal-minimal \
  "-DQBOOT_HOST_BACKEND_FAL -DQBOOT_HOST_DISABLE_GZIP -DQBOOT_HOST_DISABLE_AES -DQBOOT_HOST_DISABLE_HPATCHLITE -Wno-unused-parameter" \
  "src/qboot_fal_ops.c tests/host/qboot_host_fal.c" \
  "" \
  1
add_backend fal-gzip \
  "-DQBOOT_HOST_BACKEND_FAL -DQBOOT_HOST_DISABLE_AES -DQBOOT_HOST_DISABLE_HPATCHLITE -Wno-unused-parameter" \
  "src/qboot_fal_ops.c tests/host/qboot_host_fal.c" \
  "" \
  1
add_backend fal-gzip-only \
  "-DQBOOT_HOST_BACKEND_FAL -DQBOOT_HOST_DISABLE_AES -DQBOOT_HOST_DISABLE_HPATCHLITE -Wno-unused-parameter" \
  "src/qboot_fal_ops.c tests/host/qboot_host_fal.c" \
  "" \
  1
add_backend fs \
  "-DQBOOT_HOST_BACKEND_FS" \
  "src/qboot_fs_ops.c" \
  "" \
  1
add_backend fs-minimal \
  "-DQBOOT_HOST_BACKEND_FS -DQBOOT_HOST_DISABLE_GZIP -DQBOOT_HOST_DISABLE_AES -DQBOOT_HOST_DISABLE_HPATCHLITE -Wno-unused-parameter" \
  "src/qboot_fs_ops.c" \
  "" \
  1
add_backend fs-gzip \
  "-DQBOOT_HOST_BACKEND_FS -DQBOOT_HOST_DISABLE_AES -DQBOOT_HOST_DISABLE_HPATCHLITE -Wno-unused-parameter" \
  "src/qboot_fs_ops.c" \
  "" \
  1
add_backend fs-gzip-only \
  "-DQBOOT_HOST_BACKEND_FS -DQBOOT_HOST_DISABLE_AES -DQBOOT_HOST_DISABLE_HPATCHLITE -Wno-unused-parameter" \
  "src/qboot_fs_ops.c" \
  "" \
  1
add_backend fal-missing-package \
  "-DQBOOT_HOST_BACKEND_FAL" \
  "src/qboot_fal_ops.c" \
  "" \
  1
add_backend fs-missing-dfs \
  "-DQBOOT_HOST_BACKEND_FS -DQBOOT_HOST_MISSING_DFS_PACKAGE" \
  "src/qboot_fs_ops.c" \
  "" \
  1
add_backend custom-hpatch-missing-lib \
  "-DQBOOT_HOST_BACKEND_CUSTOM -Wno-unused-parameter" \
  "src/qboot_custom_ops.c" \
  "tests/host/qboot_host_hpatchlite.c" \
  1
add_backend custom-aes-missing-lib \
  "-DQBOOT_HOST_BACKEND_CUSTOM" \
  "src/qboot_custom_ops.c" \
  "tests/host/qboot_host_tinycrypt.c" \
  1

read_backend_config() {
  local requested=$1 row

  for row in "${backend_configs[@]}"; do
    IFS='|' read -r backend_name backend_cflags backend_sources backend_excludes backend_use_package_override <<< "$row"
    if [ "$backend_name" = "$requested" ]; then
      return 0
    fi
  done

  return 1
}

append_backend_cflags() {
  local words=$1
  local parsed=()

  [ -n "$words" ] || return 0
  read -r -a parsed <<< "$words"
  extra_cflags+=("${parsed[@]}")
}

append_backend_sources() {
  local words=$1
  local parsed=()

  [ -n "$words" ] || return 0
  read -r -a parsed <<< "$words"
  extra_sources+=("${parsed[@]}")
}

append_backend_excludes() {
  local words=$1
  local parsed=()

  [ -n "$words" ] || return 0
  read -r -a parsed <<< "$words"
  source_excludes+=("${parsed[@]}")
}

source_is_excluded() {
  local source=$1 excluded

  for excluded in "${source_excludes[@]}"; do
    if [ "$source" = "$excluded" ]; then
      return 0
    fi
  done

  return 1
}

copy_common_sources() {
  local src

  sources=()
  for src in "${common_sources[@]}"; do
    if ! source_is_excluded "$src"; then
      sources+=("$src")
    fi
  done
}

build_one() {
  local backend=$1
  local output="$out_dir/qboot_host_runner_$backend"
  local backend_name backend_cflags backend_sources backend_excludes backend_use_package_override
  local sources=() extra_cflags=() extra_sources=() source_excludes=()

  if ! read_backend_config "$backend"; then
    echo "unsupported backend: $backend" >&2
    exit 1
  fi

  append_backend_cflags "$backend_cflags"
  append_backend_sources "$backend_sources"
  append_backend_excludes "$backend_excludes"
  copy_common_sources

  if [ "$backend_use_package_override" = "1" ]; then
    extra_cflags=(-DQBOOT_CI_HOST_RBL_PACKAGE_TEST "${extra_cflags[@]}")
  fi

  "$cc" "${common_cflags[@]}" "${extra_cflags[@]}" \
    "${sources[@]}" "${extra_sources[@]}" \
    "${common_ldflags[@]}" -lz -o "$output"
  echo "Built $output"
}

for backend in ${QBOOT_HOST_BACKENDS:-$default_backends}; do
  build_one "$backend"
done
