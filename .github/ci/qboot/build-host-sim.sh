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

build_one() {
  backend=$1
  output="$out_dir/qboot_host_runner_$backend"
  sources=("${common_sources[@]}")
  case "$backend" in
    custom-helper)
      extra_cflags=(-DQBOOT_HOST_BACKEND_CUSTOM)
      extra_sources=(src/qboot_custom_ops.c)
      ;;
    custom)
      extra_cflags=(-DQBOOT_HOST_BACKEND_CUSTOM)
      extra_sources=(src/qboot_custom_ops.c)
      ;;
    custom-smallbuf)
      extra_cflags=(-DQBOOT_HOST_BACKEND_CUSTOM -DQBOOT_BUF_SIZE=16)
      extra_sources=(src/qboot_custom_ops.c)
      ;;
    custom-no-gzip)
      extra_cflags=(-DQBOOT_HOST_BACKEND_CUSTOM -DQBOOT_HOST_DISABLE_GZIP)
      extra_sources=(src/qboot_custom_ops.c)
      ;;
    custom-no-aes)
      extra_cflags=(-DQBOOT_HOST_BACKEND_CUSTOM -DQBOOT_HOST_DISABLE_AES)
      extra_sources=(src/qboot_custom_ops.c)
      ;;
    custom-no-hpatch)
      extra_cflags=(-DQBOOT_HOST_BACKEND_CUSTOM -DQBOOT_HOST_DISABLE_HPATCHLITE -Wno-unused-parameter)
      extra_sources=(src/qboot_custom_ops.c)
      ;;
    custom-gzip-only)
      extra_cflags=(-DQBOOT_HOST_BACKEND_CUSTOM -DQBOOT_HOST_DISABLE_AES -DQBOOT_HOST_DISABLE_HPATCHLITE -Wno-unused-parameter)
      extra_sources=(src/qboot_custom_ops.c)
      ;;
    custom-aes-gzip-only)
      extra_cflags=(-DQBOOT_HOST_BACKEND_CUSTOM -DQBOOT_HOST_DISABLE_HPATCHLITE -Wno-unused-parameter)
      extra_sources=(src/qboot_custom_ops.c)
      ;;
    custom-hpatch-only)
      extra_cflags=(-DQBOOT_HOST_BACKEND_CUSTOM -DQBOOT_HOST_DISABLE_GZIP -DQBOOT_HOST_DISABLE_AES -Wno-unused-parameter)
      extra_sources=(src/qboot_custom_ops.c)
      ;;
    custom-hpatch-production)
      extra_cflags=(-DQBOOT_HOST_BACKEND_CUSTOM -DQBOOT_HOST_DISABLE_GZIP -DQBOOT_HOST_DISABLE_AES -Wno-unused-parameter)
      sources=()
      for src in "${common_sources[@]}"; do
        [ "$src" = "tests/host/qboot_host_hpatchlite.c" ] || sources+=("$src")
      done
      extra_sources=(src/qboot_custom_ops.c algorithm/qboot_hpatchlite.c .github/ci/qboot/packages/hpatchlite/hpatch_impl.c)
      ;;
    custom-hpatch-storage-swap)
      extra_cflags=(-DQBOOT_HOST_BACKEND_CUSTOM -DQBOOT_HOST_HPATCH_STORAGE_SWAP -DQBOOT_HOST_DISABLE_GZIP -DQBOOT_HOST_DISABLE_AES -Wno-unused-parameter)
      sources=()
      for src in "${common_sources[@]}"; do
        [ "$src" = "tests/host/qboot_host_hpatchlite.c" ] || sources+=("$src")
      done
      extra_sources=(src/qboot_custom_ops.c algorithm/qboot_hpatchlite.c .github/ci/qboot/packages/hpatchlite/hpatch_impl.c)
      ;;
    custom-minimal)
      extra_cflags=(-DQBOOT_HOST_BACKEND_CUSTOM -DQBOOT_HOST_DISABLE_GZIP -DQBOOT_HOST_DISABLE_AES -DQBOOT_HOST_DISABLE_HPATCHLITE -Wno-unused-parameter)
      extra_sources=(src/qboot_custom_ops.c)
      ;;
    mixed-backend)
      extra_cflags=(-DQBOOT_HOST_BACKEND_MIXED)
      extra_sources=(src/qboot_custom_ops.c src/qboot_fal_ops.c src/qboot_fs_ops.c src/qboot_mux_ops.c tests/host/qboot_host_fal.c)
      ;;
    none-disabled)
      extra_cflags=(-DQBOOT_HOST_BACKEND_NONE)
      extra_sources=()
      ;;
    fal)
      extra_cflags=(-DQBOOT_HOST_BACKEND_FAL)
      extra_sources=(src/qboot_fal_ops.c tests/host/qboot_host_fal.c)
      ;;
    fal-minimal)
      extra_cflags=(-DQBOOT_HOST_BACKEND_FAL -DQBOOT_HOST_DISABLE_GZIP -DQBOOT_HOST_DISABLE_AES -DQBOOT_HOST_DISABLE_HPATCHLITE -Wno-unused-parameter)
      extra_sources=(src/qboot_fal_ops.c tests/host/qboot_host_fal.c)
      ;;
    fal-gzip|fal-gzip-only)
      extra_cflags=(-DQBOOT_HOST_BACKEND_FAL -DQBOOT_HOST_DISABLE_AES -DQBOOT_HOST_DISABLE_HPATCHLITE -Wno-unused-parameter)
      extra_sources=(src/qboot_fal_ops.c tests/host/qboot_host_fal.c)
      ;;
    fs)
      extra_cflags=(-DQBOOT_HOST_BACKEND_FS)
      extra_sources=(src/qboot_fs_ops.c)
      ;;
    fs-minimal)
      extra_cflags=(-DQBOOT_HOST_BACKEND_FS -DQBOOT_HOST_DISABLE_GZIP -DQBOOT_HOST_DISABLE_AES -DQBOOT_HOST_DISABLE_HPATCHLITE -Wno-unused-parameter)
      extra_sources=(src/qboot_fs_ops.c)
      ;;
    fs-gzip|fs-gzip-only)
      extra_cflags=(-DQBOOT_HOST_BACKEND_FS -DQBOOT_HOST_DISABLE_AES -DQBOOT_HOST_DISABLE_HPATCHLITE -Wno-unused-parameter)
      extra_sources=(src/qboot_fs_ops.c)
      ;;
    fal-missing-package)
      extra_cflags=(-DQBOOT_HOST_BACKEND_FAL)
      extra_sources=(src/qboot_fal_ops.c)
      ;;
    fs-missing-dfs)
      extra_cflags=(-DQBOOT_HOST_BACKEND_FS -DQBOOT_HOST_MISSING_DFS_PACKAGE)
      extra_sources=(src/qboot_fs_ops.c)
      ;;
    custom-hpatch-missing-lib)
      extra_cflags=(-DQBOOT_HOST_BACKEND_CUSTOM -Wno-unused-parameter)
      sources=()
      for src in "${common_sources[@]}"; do
        [ "$src" = "tests/host/qboot_host_hpatchlite.c" ] || sources+=("$src")
      done
      extra_sources=(src/qboot_custom_ops.c)
      ;;
    custom-aes-missing-lib)
      extra_cflags=(-DQBOOT_HOST_BACKEND_CUSTOM)
      sources=()
      for src in "${common_sources[@]}"; do
        [ "$src" = "tests/host/qboot_host_tinycrypt.c" ] || sources+=("$src")
      done
      extra_sources=(src/qboot_custom_ops.c)
      ;;
    *)
      echo "unsupported backend: $backend" >&2
      exit 1
      ;;
  esac
  # custom-helper intentionally keeps the production weak helper fallback
  # enabled. Other runners use host RBL package overrides.
  if [ "$backend" != "custom-helper" ]; then
    extra_cflags=(-DQBOOT_CI_HOST_RBL_PACKAGE_TEST "${extra_cflags[@]}")
  fi
  "$cc" "${common_cflags[@]}" "${extra_cflags[@]}" \
    "${sources[@]}" "${extra_sources[@]}" \
    "${common_ldflags[@]}" -lz -o "$output"
  echo "Built $output"
}

for backend in ${QBOOT_HOST_BACKENDS:-custom custom-smallbuf fal fs custom-helper custom-hpatch-production custom-hpatch-storage-swap}; do
  build_one "$backend"
done
