#!/usr/bin/env sh
set -eu

REPO_ROOT=${GITHUB_WORKSPACE:-$(pwd)}
GENERATOR=${GENERATOR:-$REPO_ROOT/.github/ci/qboot/generate-matrix-profile.sh}
WORK_DIR=${TMPDIR:-/tmp}/qboot-matrix-profile-test-$$

cleanup()
{
    rm -rf "$WORK_DIR"
}

trap cleanup EXIT HUP INT TERM
mkdir -p "$WORK_DIR"

expect_success()
{
    backend=$1
    algorithm=$2
    output_file=$3

    sh "$GENERATOR" "$backend" "$algorithm" off "$output_file" >/dev/null
}

expect_rejected()
{
    backend=$1
    algorithm=$2
    output_file=$3

    if sh "$GENERATOR" "$backend" "$algorithm" off "$output_file" >/dev/null 2>&1; then
        printf 'Expected profile rejection: backend=%s algorithm=%s\n' "$backend" "$algorithm" >&2
        return 1
    fi

    if [ -e "$output_file" ]; then
        printf 'Rejected profile unexpectedly produced output: %s\n' "$output_file" >&2
        return 1
    fi
}

expect_success fal hpatch-storage "$WORK_DIR/fal-hpatch-storage.h"
expect_success custom none "$WORK_DIR/custom-none.h"
expect_rejected fs hpatch-storage "$WORK_DIR/fs-hpatch-storage.h"
expect_rejected custom hpatch-storage "$WORK_DIR/custom-hpatch-storage.h"

grep -F '#define QBOOT_HPATCH_SWAP_STORE_FAL' "$WORK_DIR/fal-hpatch-storage.h" >/dev/null
grep -F '#define QBOOT_PKG_SOURCE_CUSTOM' "$WORK_DIR/custom-none.h" >/dev/null

printf 'QBOOT_MATRIX_PROFILE_CONSTRAINTS_PASS\n'
