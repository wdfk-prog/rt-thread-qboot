#!/usr/bin/env bash
set -euo pipefail

profile="${1:?missing qboot CI profile}"
profiles_dir=".github/ci/qboot/profiles"
profile_file="$profiles_dir/$profile.h"
common_profile="$profiles_dir/common.h"
base_root="_ci/rt-thread-base"
build_root="_ci/profile-builds/$profile/rt-thread"
bsp_dir="$build_root/${STM32_BSP:?STM32_BSP is required}"
packages_to_link="crclib qboot"

append_profile() {
  src="$1"
  dst="$2"

  if [ ! -f "$src" ]; then
    echo "missing profile fragment: $src" >&2
    exit 1
  fi

  cat >> "$dst" <<EOF_PROFILE_HEADER

/* Begin qboot CI profile fragment: $src. */
EOF_PROFILE_HEADER
  cat "$src" >> "$dst"
  cat >> "$dst" <<EOF_PROFILE_FOOTER
/* End qboot CI profile fragment: $src. */
EOF_PROFILE_FOOTER
}

resolve_packages_to_link() {
  case "$profile" in
    stm32f407-custom-algo-basic)
      packages_to_link="crclib tinycrypt zlib quicklz fastlz qboot"
      ;;
    stm32f407-hpatch-custom-swap|\
    stm32f407-hpatch-ram-buffer|\
    stm32f407-hpatch-fal-swap|\
    stm32f407-hpatch-fs-swap)
      packages_to_link="crclib hpatchlite qboot"
      ;;
    stm32f407-custom-ui-features)
      packages_to_link="crclib qled qboot"
      ;;
    *)
      packages_to_link="crclib qboot"
      ;;
  esac
}

profile_needs_fal_cfg() {
  case "$profile" in
    stm32f407-fal-backend|\
    stm32f407-mixed-app-fal-download-fs|\
    stm32f407-hpatch-fal-swap)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

integrate_packages() {
  export CI_QBOOT_PACKAGES="$packages_to_link"
  export CI_PROFILE_BSP_DIR="$bsp_dir"

  python3 -S - <<'PY'
import os
from pathlib import Path

bsp_sconscript = Path(os.environ['CI_PROFILE_BSP_DIR']) / 'SConscript'
text = bsp_sconscript.read_text(encoding='utf-8')
marker = "Return('objs')"
packages = os.environ['CI_QBOOT_PACKAGES'].split()
insert_lines = ["", "# CI-only package integration for qboot compile verification."]
for package in packages:
    insert_lines.append(f"objs = objs + SConscript('packages/{package}/SConscript')")
insertion = "\n".join(insert_lines) + "\n"
if marker not in text:
    raise SystemExit(f'{bsp_sconscript}: missing {marker}')
if 'packages/qboot/SConscript' not in text:
    text = text.replace(marker, insertion + '\n' + marker)
bsp_sconscript.write_text(text, encoding='utf-8')
PY
}

clean_qboot_profile_macros() {
  rtconfig="$1"

  sed -i '/^#define[[:space:]]\+PKG_USING_QBOOT$/d' "$rtconfig"
  sed -i '/^#define[[:space:]]\+PKG_USING_CRCLIB$/d' "$rtconfig"
  sed -i '/^#define[[:space:]]\+CRCLIB_USING_CRC32$/d' "$rtconfig"
  sed -i '/^#define[[:space:]]\+CRC32_USING_CONST_TABLE$/d' "$rtconfig"
  sed -i '/^#define[[:space:]]\+CRC32_POLY_/d' "$rtconfig"
  sed -i '/^#define[[:space:]]\+CRC32_POLY[[:space:]]/d' "$rtconfig"
  sed -i '/^#define[[:space:]]\+QBOOT_/d' "$rtconfig"
  sed -i '/^#define[[:space:]]\+QBT_/d' "$rtconfig"
  sed -i '/^#define[[:space:]]\+PKG_USING_TINYCRYPT$/d' "$rtconfig"
  sed -i '/^#define[[:space:]]\+TINY_CRYPT_/d' "$rtconfig"
  sed -i '/^#define[[:space:]]\+PKG_USING_ZLIB$/d' "$rtconfig"
  sed -i '/^#define[[:space:]]\+PKG_USING_QUICKLZ$/d' "$rtconfig"
  sed -i '/^#define[[:space:]]\+PKG_USING_FASTLZ$/d' "$rtconfig"
  sed -i '/^#define[[:space:]]\+PKG_USING_HPATCHLITE$/d' "$rtconfig"
  sed -i '/^#define[[:space:]]\+PKG_USING_QLED$/d' "$rtconfig"
  sed -i '/^#define[[:space:]]\+QLED_/d' "$rtconfig"
  sed -i '/^#define[[:space:]]\+RT_USING_FAL$/d' "$rtconfig"
  sed -i '/^#define[[:space:]]\+FAL_/d' "$rtconfig"
  sed -i '/^#define[[:space:]]\+BSP_USING_ON_CHIP_FLASH$/d' "$rtconfig"
  sed -i '/^#define[[:space:]]\+RT_USING_DFS$/d' "$rtconfig"
  sed -i '/^#define[[:space:]]\+DFS_/d' "$rtconfig"
  sed -i '/^#define[[:space:]]\+RT_USING_DFS_/d' "$rtconfig"
  sed -i '/^#define[[:space:]]\+RT_USING_MSH$/d' "$rtconfig"
  sed -i '/^#define[[:space:]]\+RT_USING_FINSH$/d' "$rtconfig"
  sed -i '/^#define[[:space:]]\+FINSH_/d' "$rtconfig"
}

if [ ! -f "$profile_file" ]; then
  echo "unsupported qboot CI profile: $profile" >&2
  exit 1
fi

if [ ! -d "$base_root" ]; then
  echo "missing shared RT-Thread base: $base_root" >&2
  exit 1
fi

rm -rf "_ci/profile-builds/$profile"
mkdir -p "_ci/profile-builds/$profile"
rsync -a --delete "$base_root/" "$build_root/"

test -f "$bsp_dir/SConstruct"
test -f "$bsp_dir/rtconfig.h"

resolve_packages_to_link

if profile_needs_fal_cfg; then
  mkdir -p "$bsp_dir/applications"
  cp .github/ci/qboot/fal_cfg.h "$bsp_dir/applications/fal_cfg.h"
fi

integrate_packages

rtconfig="$bsp_dir/rtconfig.h"
clean_qboot_profile_macros "$rtconfig"
append_profile "$common_profile" "$rtconfig"
append_profile "$profile_file" "$rtconfig"

echo "Configured qboot CI profile: $profile"
echo "Linked RT-Thread packages: $packages_to_link"
grep -E '^(#define[[:space:]]+(QBOOT|QBT|PKG_USING|CRCLIB|CRC32|TINY_CRYPT|RT_USING|DFS_|FAL_|BSP_USING_ON_CHIP_FLASH|QLED|FINSH))' \
  "$rtconfig" || true

export RTT_CC=gcc
export RTT_EXEC_PATH=/usr/bin
export RTT_ROOT="$GITHUB_WORKSPACE/$build_root"

cd "$RTT_ROOT/$STM32_BSP"
scons -j2

test -f rt-thread.elf
test -f rtthread.bin
arm-none-eabi-size rt-thread.elf
