#!/usr/bin/env sh
set -eu

RTTHREAD_REPOSITORY=${RTTHREAD_REPOSITORY:-https://github.com/RT-Thread/rt-thread.git}
RTTHREAD_REF=${RTTHREAD_REF:-master}
RTTHREAD_ENV_REPOSITORY=${RTTHREAD_ENV_REPOSITORY:-https://github.com/RT-Thread/env.git}
RTTHREAD_ENV_REF=${RTTHREAD_ENV_REF:-master}
RTTHREAD_PACKAGES_REPOSITORY=${RTTHREAD_PACKAGES_REPOSITORY:-https://github.com/RT-Thread/packages.git}
RTTHREAD_PACKAGES_REF=${RTTHREAD_PACKAGES_REF:-master}
RTTHREAD_BSP=${RTTHREAD_BSP:-bsp/stm32/stm32f407-atk-explorer}
CRCLIB_REPOSITORY=${CRCLIB_REPOSITORY:-https://github.com/qiyongzhong0/crclib.git}
CRCLIB_REF=${CRCLIB_REF:-v1.02}
QLED_REPOSITORY=${QLED_REPOSITORY:-https://github.com/qiyongzhong0/rt-thread-qled.git}
QLED_REF=${QLED_REF:-master}
LITTLEFS_REPOSITORY=${LITTLEFS_REPOSITORY:-https://github.com/RT-Thread-packages/littlefs.git}
LITTLEFS_REF=${LITTLEFS_REF:-master}
PROFILE_FILE=${PROFILE_FILE:-.github/ci/qboot/profiles/stm32f407-fal-fs.h}
FAL_CFG_FILE=${FAL_CFG_FILE:-.github/ci/qboot/profiles/fal_cfg.h}

WORK_ROOT=${GITHUB_WORKSPACE}/_ci/stm32f407-fal-fs
LOG_DIR=${GITHUB_WORKSPACE}/_ci/logs
ARTIFACT_DIR=${GITHUB_WORKSPACE}/_ci/artifacts/stm32f407-fal-fs
LOG_FILE=${LOG_DIR}/stm32f407-fal-fs.log
RTT_ROOT=${WORK_ROOT}/rt-thread
RTT_ENV=${WORK_ROOT}/env
BSP_DIR=${RTT_ROOT}/${RTTHREAD_BSP}
ENV_PACKAGE_INDEX=${HOME}/.env/packages/packages

mkdir -p "$WORK_ROOT" "$LOG_DIR" "$ARTIFACT_DIR"
: > "$LOG_FILE"

run_logged()
{
    step_name=$1
    shift
    step_log=${WORK_ROOT}/step.log

    printf '\n===== %s =====\n' "$step_name" | tee -a "$LOG_FILE"
    if "$@" > "$step_log" 2>&1; then
        status=0
    else
        status=$?
    fi
    cat "$step_log" | tee -a "$LOG_FILE"
    rm -f "$step_log"
    return "$status"
}

record_source_revision()
{
    repo_dir=$1
    label=$2
    sha=$(git -C "$repo_dir" rev-parse HEAD)
    printf '%s=%s\n' "$label" "$sha" | tee -a "$LOG_FILE"
}

clone_dependency()
{
    name=$1
    repository=$2
    ref=$3
    destination=$4

    rm -rf "$destination"
    run_logged "Clone ${name}" git clone --depth 1 --branch "$ref" "$repository" "$destination"
    record_source_revision "$destination" "${name}_SHA"
}

use_official_package_index()
{
    if [ ! -d "$ENV_PACKAGE_INDEX/.git" ]; then
        printf 'RT-Thread package index was not initialized: %s\n' "$ENV_PACKAGE_INDEX" >&2
        return 1
    fi

    git -C "$ENV_PACKAGE_INDEX" remote set-url origin "$RTTHREAD_PACKAGES_REPOSITORY"
    git -C "$ENV_PACKAGE_INDEX" fetch --depth 1 origin "$RTTHREAD_PACKAGES_REF"
    git -C "$ENV_PACKAGE_INDEX" checkout --detach FETCH_HEAD
}

stage_qboot()
{
    qboot_dir=$BSP_DIR/packages/qboot
    rm -rf "$qboot_dir"
    mkdir -p "$qboot_dir"
    cp "$GITHUB_WORKSPACE/SConscript" "$qboot_dir/SConscript"
    cp -R "$GITHUB_WORKSPACE/algorithm" "$qboot_dir/algorithm"
    cp -R "$GITHUB_WORKSPACE/inc" "$qboot_dir/inc"
    cp -R "$GITHUB_WORKSPACE/platform" "$qboot_dir/platform"
    cp -R "$GITHUB_WORKSPACE/src" "$qboot_dir/src"
}

stage_fal_config()
{
    fal_cfg_dir=$BSP_DIR/board/ports/fal

    if [ ! -d "$fal_cfg_dir" ]; then
        printf 'STM32 BSP FAL port directory was not found: %s\n' "$fal_cfg_dir" >&2
        return 1
    fi

    cp "$GITHUB_WORKSPACE/$FAL_CFG_FILE" "$fal_cfg_dir/fal_cfg.h"
}

verify_stm32_packages()
{
    stm32_root=$RTT_ROOT/bsp/stm32

    for header in stm32f4xx.h stm32f4xx_hal.h; do
        if ! find "$stm32_root" -type f -name "$header" -print -quit | grep -q .; then
            printf 'Required STM32 package header was not fetched: %s\n' "$header" >&2
            return 1
        fi
    done
}

collect_outputs()
{
    cd "$BSP_DIR"
    elf_file=$(find . -maxdepth 1 -type f -name 'rt-thread*.elf' -print -quit)
    if [ -z "$elf_file" ]; then
        printf 'RT-Thread ELF output was not found.\n' >&2
        return 1
    fi

    arm-none-eabi-size "$elf_file" > "$ARTIFACT_DIR/size.txt"
    cat "$ARTIFACT_DIR/size.txt"
    cp "$elf_file" "$ARTIFACT_DIR/"

    map_file=$(find . -maxdepth 1 -type f -name 'rt-thread*.map' -print -quit)
    if [ -n "$map_file" ]; then
        cp "$map_file" "$ARTIFACT_DIR/"
    fi
}

rm -rf "$RTT_ROOT" "$RTT_ENV"
run_logged 'Clone RT-Thread' \
    git clone --depth 1 --branch "$RTTHREAD_REF" "$RTTHREAD_REPOSITORY" "$RTT_ROOT"
run_logged 'Clone RT-Thread Env' \
    git clone --depth 1 --branch "$RTTHREAD_ENV_REF" "$RTTHREAD_ENV_REPOSITORY" "$RTT_ENV"

record_source_revision "$RTT_ROOT" RTTHREAD_SHA
record_source_revision "$RTT_ENV" RTTHREAD_ENV_SHA

export RTT_ROOT RTT_ENV RTT_CC RTT_EXEC_PATH
PATH="$HOME/.local/bin:$RTT_ENV:$PATH"
export PATH

if [ ! -d "$BSP_DIR" ]; then
    printf 'RT-Thread BSP not found: %s\n' "$RTTHREAD_BSP" | tee -a "$LOG_FILE" >&2
    exit 1
fi

cd "$BSP_DIR"
run_logged 'Generate BSP configuration' scons --pyconfig-silent
run_logged 'Use official RT-Thread package index' use_official_package_index
record_source_revision "$ENV_PACKAGE_INDEX" RTTHREAD_PACKAGES_SHA
run_logged 'Fetch BSP dependency packages' python "$RTT_ENV/env.py" package --update
run_logged 'Verify STM32 dependency packages' verify_stm32_packages

stage_qboot
stage_fal_config
clone_dependency CRCLIB "$CRCLIB_REPOSITORY" "$CRCLIB_REF" "$BSP_DIR/packages/crclib"
clone_dependency QLED "$QLED_REPOSITORY" "$QLED_REF" "$BSP_DIR/packages/qled"
clone_dependency LITTLEFS "$LITTLEFS_REPOSITORY" "$LITTLEFS_REF" "$BSP_DIR/packages/littlefs"

cat "$GITHUB_WORKSPACE/$PROFILE_FILE" >> "$BSP_DIR/rtconfig.h"

{
    printf '\n===== Effective QBoot CI configuration =====\n'
    grep -E \
        -e '^#define RT_USING_(DFS|FAL|SPI|SFUD|MTD_NOR)' \
        -e '^#define FAL_' \
        -e '^#define BSP_USING_(FS|SPI_FLASH|SPI|ON_CHIP_FLASH)' \
        -e '^#define PKG_USING_(QBOOT|CRCLIB|QLED|LITTLEFS)' \
        -e '^#define CRC(8|16|32)_' \
        -e '^#define QBOOT_' \
        "$BSP_DIR/rtconfig.h" || true
} | tee -a "$LOG_FILE" | tee "$ARTIFACT_DIR/effective-config.txt"

cd "$BSP_DIR"
run_logged 'Build RT-Thread with QBoot' scons -j2
collect_outputs

printf 'RTTHREAD_QBOOT_BUILD_PASS\n' | tee -a "$LOG_FILE"
cp "$LOG_FILE" "$ARTIFACT_DIR/build.log"
