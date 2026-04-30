#!/usr/bin/env bash
set -euo pipefail

repo_root="$(pwd)"
bsp_dir="_ci/rt-thread-base/${STM32_BSP:?STM32_BSP is required}"

stage_git_package() {
  pkg_name="$1"
  repo_url="$2"
  ref_name="$3"
  dst="$bsp_dir/packages/$pkg_name"

  if [ -d "$dst" ]; then
    return 0
  fi

  git clone --depth 1 --branch "$ref_name" "$repo_url" "$dst"
}

stage_builtin_hpatchlite_package() {
  dst="$bsp_dir/packages/hpatchlite"

  if [ -d "$dst" ]; then
    return 0
  fi

  mkdir -p "$bsp_dir/packages"
  cp -a .github/ci/qboot/packages/hpatchlite "$dst"
}

prune_crclib_sample_sources() {
  pkg_dir="$bsp_dir/packages/crclib"

  if [ ! -d "$pkg_dir" ]; then
    echo "missing crclib package directory: $pkg_dir" >&2
    exit 1
  fi

  find "$pkg_dir" -type f -name '*_sample.c' -print -delete
}

isolate_zlib_private_crc32_header() {
  pkg_dir="$bsp_dir/packages/zlib"
  src_file="$pkg_dir/src/crc32.c"
  src_header="$pkg_dir/src/crc32.h"
  dst_header="$pkg_dir/src/zlib_crc32_table.h"

  if [ ! -f "$src_file" ]; then
    echo "missing zlib crc32 source: $src_file" >&2
    exit 1
  fi

  if [ ! -f "$src_header" ]; then
    echo "missing zlib private crc32 header: $src_header" >&2
    exit 1
  fi

  mv "$src_header" "$dst_header"
  sed -i 's/#include "crc32.h"/#include "zlib_crc32_table.h"/' "$src_file"

  if grep -q '#include "crc32.h"' "$src_file"; then
    echo "failed to isolate zlib private crc32 header include" >&2
    exit 1
  fi
}

prepare_rtthread_env() {
  env_dir="_ci/env"

  if [ -d "$env_dir" ]; then
    return 0
  fi

  git clone --depth 1 --branch "${RTTHREAD_ENV_REF:?RTTHREAD_ENV_REF is required}" \
    https://github.com/RT-Thread/env.git "$env_dir"
}

update_bsp_packages() {
  prepare_rtthread_env

  python3 -m pip install --user kconfiglib tqdm || \
    python3 -m pip install --user --break-system-packages kconfiglib tqdm

  (
    cd "$bsp_dir"
    python3 "$repo_root/_ci/env/env.py" package --upgrade
    python3 "$repo_root/_ci/env/env.py" package --update
  )
}

rm -rf _ci/rt-thread-base _ci/profile-builds _ci/profile-logs
mkdir -p _ci

git clone --depth 1 --branch "${RTTHREAD_REF:?RTTHREAD_REF is required}" \
  https://github.com/RT-Thread/rt-thread.git _ci/rt-thread-base

test -f "$bsp_dir/SConstruct"
test -f "$bsp_dir/rtconfig.h"

update_bsp_packages

mkdir -p "$bsp_dir/packages/qboot"
rsync -a --delete \
  --exclude='.git' \
  --exclude='.github' \
  --exclude='_ci' \
  ./ "$bsp_dir/packages/qboot/"

stage_git_package crclib https://github.com/qiyongzhong0/crclib.git "$CRCLIB_REF"
prune_crclib_sample_sources
stage_git_package tinycrypt https://github.com/RT-Thread-packages/tinycrypt.git "$TINYCRYPT_REF"
stage_git_package zlib https://github.com/RT-Thread-packages/zlib.git "$ZLIB_REF"
isolate_zlib_private_crc32_header
stage_git_package quicklz https://github.com/RT-Thread-packages/quicklz.git "$QUICKLZ_REF"
stage_git_package fastlz https://github.com/RT-Thread-packages/fastlz.git "$FASTLZ_REF"
stage_git_package qled https://github.com/qiyongzhong0/rt-thread-qled.git "$QLED_REF"
stage_builtin_hpatchlite_package

echo "Prepared shared RT-Thread BSP base: _ci/rt-thread-base/$STM32_BSP"
