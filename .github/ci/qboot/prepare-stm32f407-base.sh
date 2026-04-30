#!/usr/bin/env bash
set -euo pipefail

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

rm -rf _ci/rt-thread-base _ci/profile-builds _ci/profile-logs
mkdir -p _ci

git clone --depth 1 --branch "${RTTHREAD_REF:?RTTHREAD_REF is required}" \
  https://github.com/RT-Thread/rt-thread.git _ci/rt-thread-base

test -f "$bsp_dir/SConstruct"
test -f "$bsp_dir/rtconfig.h"

mkdir -p "$bsp_dir/packages/qboot"
rsync -a --delete \
  --exclude='.git' \
  --exclude='.github' \
  --exclude='_ci' \
  ./ "$bsp_dir/packages/qboot/"

stage_git_package crclib https://github.com/qiyongzhong0/crclib.git "$CRCLIB_REF"
stage_git_package tinycrypt https://github.com/RT-Thread-packages/tinycrypt.git "$TINYCRYPT_REF"
stage_git_package zlib https://github.com/RT-Thread-packages/zlib.git "$ZLIB_REF"
stage_git_package quicklz https://github.com/RT-Thread-packages/quicklz.git "$QUICKLZ_REF"
stage_git_package fastlz https://github.com/RT-Thread-packages/fastlz.git "$FASTLZ_REF"
stage_git_package qled https://github.com/qiyongzhong0/rt-thread-qled.git "$QLED_REF"
stage_builtin_hpatchlite_package

echo "Prepared shared RT-Thread BSP base: _ci/rt-thread-base/$STM32_BSP"
