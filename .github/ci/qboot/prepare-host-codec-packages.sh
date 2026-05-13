#!/usr/bin/env bash
set -euo pipefail

pkg_root="${QBOOT_HOST_CODEC_PACKAGE_DIR:-_ci/host-codecs/packages}"
selected_packages="${QBOOT_HOST_CODEC_PACKAGES:-crclib tinycrypt zlib quicklz fastlz hpatchlite}"

validate_selected_packages() {
  local pkg_name

  if [ -z "${selected_packages//[[:space:]]/}" ]; then
    echo "QBOOT_HOST_CODEC_PACKAGES must not be empty" >&2
    exit 1
  fi

  for pkg_name in $selected_packages; do
    case "$pkg_name" in
      crclib|tinycrypt|zlib|quicklz|fastlz|hpatchlite) ;;
      *)
        echo "invalid QBOOT_HOST_CODEC_PACKAGES entry: $pkg_name" >&2
        exit 1
        ;;
    esac
  done
}

should_stage_package() {
  local wanted=$1 pkg_name

  for pkg_name in $selected_packages; do
    if [ "$pkg_name" = "$wanted" ]; then
      return 0
    fi
  done
  return 1
}

stage_selected_git_package() {
  local pkg_name=$1

  if should_stage_package "$pkg_name"; then
    stage_git_package "$@"
  fi
}

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

  # shellcheck disable=SC2016 # apostrophe in couldn't is a literal grep pattern
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
  dst="$pkg_root/$pkg_name"
  tmp_log="$pkg_root/$pkg_name.clone.log"

  mkdir -p "$pkg_root"

  if [ -d "$dst/.git" ]; then
    : > "$tmp_log"
    checkout_git_package_ref "$pkg_name" "$repo_url" "$ref_name" "$dst" "$tmp_log"
    return $?
  fi
  if [ -e "$dst" ]; then
    echo "refusing to reuse non-git package path: $dst" >&2
    return 1
  fi

  if git clone --depth 1 "$repo_url" "$dst" > "$tmp_log" 2>&1 && \
    checkout_git_package_ref "$pkg_name" "$repo_url" "$ref_name" "$dst" "$tmp_log"; then
    return 0
  fi

  cat "$tmp_log" >&2
  return 1
}

validate_selected_packages

stage_selected_git_package crclib https://github.com/qiyongzhong0/crclib.git "${CRCLIB_REF:-master}"
stage_selected_git_package tinycrypt https://github.com/RT-Thread-packages/tinycrypt.git "${TINYCRYPT_REF:-master}"
stage_selected_git_package zlib https://github.com/RT-Thread-packages/zlib.git "${ZLIB_REF:-master}"
stage_selected_git_package quicklz https://github.com/RT-Thread-packages/quicklz.git "${QUICKLZ_REF:-master}"
stage_selected_git_package fastlz https://github.com/RT-Thread-packages/fastlz.git "${FASTLZ_REF:-master}"
stage_selected_git_package hpatchlite https://github.com/wdfk-prog/hpatchlite-wrapper.git "${HPATCHLITE_REF:-master}"

printf 'Prepared host codec packages: %s (%s)\n' "$pkg_root" "$selected_packages"
