#!/usr/bin/env bash
set -euo pipefail

repo_root="$(pwd)"
bsp_dir="_ci/rt-thread-base/${STM32_BSP:?STM32_BSP is required}"

update_git_submodules() {
  local pkg_name=$1 dst=$2 tmp_log=$3 submodule_log

  if [ ! -f "$dst/.gitmodules" ]; then
    return 0
  fi

  submodule_log="$tmp_log.submodules"
  if git -C "$dst" submodule update --init --recursive --depth 1 > "$submodule_log" 2>&1; then
    return 0
  fi

  echo "failed to update submodules for $pkg_name" >&2
  cat "$submodule_log" >&2
  return 1
}

checkout_git_package_ref() {
  local pkg_name=$1 repo_url=$2 ref_name=$3 dst=$4 tmp_log=$5

  if ! git -C "$dst" remote set-url origin "$repo_url" >> "$tmp_log" 2>&1; then
    cat "$tmp_log" >&2
    return 1
  fi

  if git -C "$dst" fetch --depth 1 origin "$ref_name" >> "$tmp_log" 2>&1 && \
    git -C "$dst" checkout --detach FETCH_HEAD >> "$tmp_log" 2>&1; then
    update_git_submodules "$pkg_name" "$dst" "$tmp_log"
    return $?
  fi

  if [ "$ref_name" = "master" ] && \
    grep -Eq "Remote branch .* not found|could not find remote ref|couldn't find remote ref|not our ref" "$tmp_log"; then
    printf 'ref `%s` is unavailable for %s; retrying repository default branch\n' "$ref_name" "$pkg_name" >&2
    cat "$tmp_log" >&2
    : > "$tmp_log"
    if git -C "$dst" fetch --depth 1 origin HEAD >> "$tmp_log" 2>&1 && \
      git -C "$dst" checkout --detach FETCH_HEAD >> "$tmp_log" 2>&1; then
      update_git_submodules "$pkg_name" "$dst" "$tmp_log"
      return $?
    fi
  fi

  cat "$tmp_log" >&2
  return 1
}

stage_git_package() {
  local pkg_name=$1 repo_url=$2 ref_name=$3 dst tmp_log
  dst="$bsp_dir/packages/$pkg_name"
  tmp_log="$bsp_dir/packages/$pkg_name.clone.log"

  mkdir -p "$bsp_dir/packages"

  if [ -d "$dst/.git" ]; then
    : > "$tmp_log"
    checkout_git_package_ref "$pkg_name" "$repo_url" "$ref_name" "$dst" "$tmp_log"
    return $?
  fi
  if [ -e "$dst" ]; then
    echo "refusing to reuse non-git package path: $dst" >&2
    return 1
  fi

  if git clone --depth 1 --branch "$ref_name" "$repo_url" "$dst" > "$tmp_log" 2>&1; then
    update_git_submodules "$pkg_name" "$dst" "$tmp_log"
    return $?
  fi

  if [ "$ref_name" = "master" ] && \
    grep -Eq "Remote branch .* not found|could not find remote ref|couldn't find remote ref|not our ref" "$tmp_log"; then
    printf 'ref `%s` is unavailable for %s; retrying repository default branch\n' "$ref_name" "$pkg_name" >&2
    cat "$tmp_log" >&2
    rm -rf "$dst"
    if git clone --depth 1 "$repo_url" "$dst" > "$tmp_log" 2>&1; then
      update_git_submodules "$pkg_name" "$dst" "$tmp_log"
      return $?
    fi
  fi

  cat "$tmp_log" >&2
  return 1
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

# Keep _ci/profile-logs intact because the workflow may tee this script's
# output to _ci/profile-logs/prepare-stm32f407-base.log while the
# script is running. Removing the directory here leaves tee writing to
# an unlinked file, so the follow-up upload-artifact step has no file
# to upload.
rm -rf _ci/rt-thread-base _ci/profile-builds
mkdir -p _ci _ci/profile-logs

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
stage_git_package hpatchlite https://github.com/wdfk-prog/hpatchlite-wrapper.git "$HPATCHLITE_REF"

echo "Prepared shared RT-Thread BSP base: _ci/rt-thread-base/$STM32_BSP"
