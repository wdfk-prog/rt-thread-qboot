#!/usr/bin/env bash
set -euo pipefail

out_dir="_ci/host-sim/sanitizer"
mkdir -p "$out_dir"
log_file="$out_dir/asan-ubsan.log"

QBOOT_HOST_SANITIZER=1 bash .github/ci/qboot/build-host-sim.sh > "$out_dir/build.log" 2>&1
ASAN_OPTIONS="detect_leaks=1:halt_on_error=1" \
UBSAN_OPTIONS="halt_on_error=1" \
bash .github/ci/qboot/run-host-sim.sh > "$log_file" 2>&1
printf 'QBOOT_HOST_SANITIZER_PASS asan-ubsan-lsan\n'
printf '# QBoot Host Sanitizer Report\n\nASAN/UBSAN/LSAN host simulation passed.\n\n- Build log: `%s`\n- Run log: `%s`\n' "$out_dir/build.log" "$log_file" > "$out_dir/sanitizer_summary.md"
