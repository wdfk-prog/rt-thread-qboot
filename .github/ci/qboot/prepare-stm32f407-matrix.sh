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
TINYCRYPT_REPOSITORY=${TINYCRYPT_REPOSITORY:-https://github.com/RT-Thread-packages/tinycrypt.git}
TINYCRYPT_REF=${TINYCRYPT_REF:-master}
ZLIB_REPOSITORY=${ZLIB_REPOSITORY:-https://github.com/RT-Thread-packages/zlib.git}
ZLIB_REF=${ZLIB_REF:-master}
QUICKLZ_REPOSITORY=${QUICKLZ_REPOSITORY:-https://github.com/RT-Thread-packages/quicklz.git}
QUICKLZ_REF=${QUICKLZ_REF:-master}
FASTLZ_REPOSITORY=${FASTLZ_REPOSITORY:-https://github.com/RT-Thread-packages/fastlz.git}
FASTLZ_REF=${FASTLZ_REF:-master}
HPATCHLITE_REPOSITORY=${HPATCHLITE_REPOSITORY:-https://github.com/sulfurandcu/hpatchlite-wrapper.git}
HPATCHLITE_REF=${HPATCHLITE_REF:-main}

WORK_ROOT=${GITHUB_WORKSPACE}/_ci/qboot-matrix-prepare
PREPARED_DIR=${GITHUB_WORKSPACE}/_ci/prepared
LOG_DIR=${GITHUB_WORKSPACE}/_ci/logs
LOG_FILE=${LOG_DIR}/qboot-matrix-prepare.log
RTT_ROOT=${WORK_ROOT}/rt-thread
RTT_ENV=${WORK_ROOT}/env
BSP_DIR=${RTT_ROOT}/${RTTHREAD_BSP}
ENV_PACKAGE_INDEX=${HOME}/.env/packages/packages

mkdir -p "$WORK_ROOT" "$PREPARED_DIR" "$LOG_DIR"
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

clone_recursive_dependency()
{
    name=$1
    repository=$2
    ref=$3
    destination=$4

    rm -rf "$destination"
    run_logged "Clone ${name}" git clone --depth 1 --branch "$ref" --recurse-submodules --shallow-submodules \
        "$repository" "$destination"
    record_source_revision "$destination" "${name}_SHA"
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

clone_dependency CRCLIB "$CRCLIB_REPOSITORY" "$CRCLIB_REF" "$BSP_DIR/packages/crclib"
clone_dependency TINYCRYPT "$TINYCRYPT_REPOSITORY" "$TINYCRYPT_REF" "$BSP_DIR/packages/tinycrypt"
clone_dependency ZLIB "$ZLIB_REPOSITORY" "$ZLIB_REF" "$BSP_DIR/packages/zlib"
clone_dependency QUICKLZ "$QUICKLZ_REPOSITORY" "$QUICKLZ_REF" "$BSP_DIR/packages/quicklz"
clone_dependency FASTLZ "$FASTLZ_REPOSITORY" "$FASTLZ_REF" "$BSP_DIR/packages/fastlz"
clone_recursive_dependency HPATCHLITE "$HPATCHLITE_REPOSITORY" "$HPATCHLITE_REF" \
    "$BSP_DIR/packages/hpatchlite-wrapper"

rm -f "$PREPARED_DIR/qboot-matrix-source.tar.gz"
run_logged 'Archive prepared RT-Thread source' \
    tar -czf "$PREPARED_DIR/qboot-matrix-source.tar.gz" -C "$WORK_ROOT" rt-thread env

cp "$LOG_FILE" "$PREPARED_DIR/source-revisions.log"
printf 'QBOOT_MATRIX_PREPARE_PASS\n' | tee -a "$LOG_FILE"
