#!/usr/bin/env sh
set -eu

RTTHREAD_BSP=${RTTHREAD_BSP:-bsp/stm32/stm32f407-atk-explorer}
QBOOT_MATRIX_PROFILE=${QBOOT_MATRIX_PROFILE:?QBOOT_MATRIX_PROFILE is required}
QBOOT_MATRIX_BACKEND=${QBOOT_MATRIX_BACKEND:?QBOOT_MATRIX_BACKEND is required}
QBOOT_MATRIX_ALGORITHM=${QBOOT_MATRIX_ALGORITHM:?QBOOT_MATRIX_ALGORITHM is required}
QBOOT_MATRIX_UPDATE=${QBOOT_MATRIX_UPDATE:?QBOOT_MATRIX_UPDATE is required}
FAL_CFG_FILE=${FAL_CFG_FILE:-.github/ci/qboot/profiles/fal_cfg.h}

WORK_ROOT=${GITHUB_WORKSPACE}/_ci/qboot-matrix-${QBOOT_MATRIX_PROFILE}
PREPARED_ARCHIVE=${GITHUB_WORKSPACE}/_ci/prepared/qboot-matrix-source.tar.gz
LOG_DIR=${GITHUB_WORKSPACE}/_ci/logs
ARTIFACT_DIR=${GITHUB_WORKSPACE}/_ci/artifacts/qboot-matrix/${QBOOT_MATRIX_PROFILE}
LOG_FILE=${LOG_DIR}/qboot-matrix-${QBOOT_MATRIX_PROFILE}.log
PROFILE_FILE=${WORK_ROOT}/qboot-matrix-profile.h
RTT_ROOT=${WORK_ROOT}/rt-thread
RTT_ENV=${WORK_ROOT}/env
BSP_DIR=${RTT_ROOT}/${RTTHREAD_BSP}

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
    fal_component_dir=$RTT_ROOT/components/fal/inc
    fal_bsp_dir=$BSP_DIR/board/ports/fal

    if [ ! -d "$fal_component_dir" ]; then
        printf 'RT-Thread FAL include directory was not found: %s\n' "$fal_component_dir" >&2
        return 1
    fi
    if [ ! -d "$fal_bsp_dir" ]; then
        printf 'STM32 BSP FAL port directory was not found: %s\n' "$fal_bsp_dir" >&2
        return 1
    fi

    cp "$GITHUB_WORKSPACE/$FAL_CFG_FILE" "$fal_component_dir/fal_cfg.h"
    cp "$GITHUB_WORKSPACE/$FAL_CFG_FILE" "$fal_bsp_dir/fal_cfg.h"
}

verify_expected_sources()
{
    case "$QBOOT_MATRIX_ALGORITHM" in
        none)
            ;;
        aes)
            grep -F 'qboot_aes.o' "$LOG_FILE" >/dev/null
            ;;
        gzip)
            grep -F 'qboot_gzip.o' "$LOG_FILE" >/dev/null
            ;;
        quicklz)
            grep -F 'qboot_quicklz.o' "$LOG_FILE" >/dev/null
            ;;
        fastlz)
            grep -F 'qboot_fastlz.o' "$LOG_FILE" >/dev/null
            ;;
        hpatch-ram|hpatch-storage)
            grep -F 'qboot_hpatchlite.o' "$LOG_FILE" >/dev/null
            ;;
    esac

    if [ "$QBOOT_MATRIX_UPDATE" = off ]; then
        if grep -F 'qboot_update.o' "$LOG_FILE" >/dev/null; then
            printf 'Unexpected qboot_update.o in update-manager-off profile.\n' >&2
            return 1
        fi
    else
        grep -F 'qboot_update.o' "$LOG_FILE" >/dev/null
    fi
}

collect_outputs()
{
    cd "$BSP_DIR"
    elf_file=$(find . -type f -name 'rt-thread*.elf' -print -quit)
    if [ -z "$elf_file" ]; then
        printf 'RT-Thread ELF output was not found.\n' >&2
        return 1
    fi

    arm-none-eabi-size "$elf_file" > "$ARTIFACT_DIR/size.txt"
    cat "$ARTIFACT_DIR/size.txt"
    cp "$elf_file" "$ARTIFACT_DIR/"

    map_file=$(find . -type f -name 'rt-thread*.map' -print -quit)
    if [ -n "$map_file" ]; then
        cp "$map_file" "$ARTIFACT_DIR/"
    fi
}

if [ ! -f "$PREPARED_ARCHIVE" ]; then
    printf 'Prepared source archive not found: %s\n' "$PREPARED_ARCHIVE" >&2
    exit 1
fi

rm -rf "$RTT_ROOT" "$RTT_ENV"
run_logged 'Extract prepared RT-Thread source' tar -xzf "$PREPARED_ARCHIVE" -C "$WORK_ROOT"

export RTT_ROOT RTT_ENV RTT_CC RTT_EXEC_PATH
PATH="$HOME/.local/bin:$RTT_ENV:$PATH"
export PATH

if [ ! -d "$BSP_DIR" ]; then
    printf 'RT-Thread BSP not found after extraction: %s\n' "$BSP_DIR" >&2
    exit 1
fi

stage_qboot
if [ "$QBOOT_MATRIX_BACKEND" = fal ]; then
    stage_fal_config
fi

run_logged 'Generate QBoot matrix profile' \
    sh "$GITHUB_WORKSPACE/.github/ci/qboot/generate-matrix-profile.sh" \
        "$QBOOT_MATRIX_BACKEND" "$QBOOT_MATRIX_ALGORITHM" "$QBOOT_MATRIX_UPDATE" "$PROFILE_FILE"

cat "$PROFILE_FILE" >> "$BSP_DIR/rtconfig.h"

{
    printf '\n===== QBoot matrix selection =====\n'
    printf 'PROFILE=%s\n' "$QBOOT_MATRIX_PROFILE"
    printf 'BACKEND=%s\n' "$QBOOT_MATRIX_BACKEND"
    printf 'ALGORITHM=%s\n' "$QBOOT_MATRIX_ALGORITHM"
    printf 'UPDATE=%s\n' "$QBOOT_MATRIX_UPDATE"
    printf '\n===== Effective QBoot matrix configuration =====\n'
    grep -E \
        -e '^#define RT_USING_(DFS|FAL|SPI|SFUD|MTD_NOR)' \
        -e '^#define FAL_' \
        -e '^#define BSP_USING_(SPI_FLASH|SPI|ON_CHIP_FLASH)' \
        -e '^#define PKG_USING_(QBOOT|CRCLIB|TINYCRYPT|ZLIB|QUICKLZ|FASTLZ|HPATCHLITE)' \
        -e '^#define QBOOT_' \
        -e '^#define QBT_UPDATE_' \
        "$BSP_DIR/rtconfig.h" || true
} | tee -a "$LOG_FILE" | tee "$ARTIFACT_DIR/effective-config.txt"

cd "$BSP_DIR"
run_logged 'Build RT-Thread QBoot matrix profile' scons -j2
run_logged 'Verify selected QBoot sources were compiled' verify_expected_sources
collect_outputs

cp "$PROFILE_FILE" "$ARTIFACT_DIR/profile.h"
cp "$LOG_FILE" "$ARTIFACT_DIR/build.log"
printf 'QBOOT_MATRIX_COMPILE_PASS %s\n' "$QBOOT_MATRIX_PROFILE" | tee -a "$LOG_FILE"
