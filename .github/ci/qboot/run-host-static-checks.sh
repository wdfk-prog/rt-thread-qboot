#!/usr/bin/env bash
set -euo pipefail

out_dir="_ci/host-sim/static-checks"
mkdir -p "$out_dir"
summary="$out_dir/static_checks_summary.md"

QBOOT_HOST_ANALYZER=1 QBOOT_HOST_BACKENDS="custom" \
  bash .github/ci/qboot/build-host-sim.sh > "$out_dir/gcc-fanalyzer.log" 2>&1

cppcheck_result=SKIPPED
if command -v cppcheck >/dev/null 2>&1; then
  cppcheck_status=0
  cppcheck --enable=warning,style,performance,portability --inconclusive \
    --std=c11 --force --quiet \
    --suppress=missingInclude --suppress=missingIncludeSystem \
    -Iinc -Itests/host -Itests/host/stubs src algorithm tests/host \
    2> "$out_dir/cppcheck.txt" || cppcheck_status=$?
  if grep -Eq '^[^:]+:[0-9]+:[0-9]+: error:' "$out_dir/cppcheck.txt"; then
    printf 'QBOOT_HOST_STATIC_CHECK_FAIL cppcheck-error\n'
    cat "$out_dir/cppcheck.txt"
    exit 1
  fi
  if [ "$cppcheck_status" -ne 0 ]; then
    printf 'QBOOT_HOST_STATIC_CHECK_FAIL cppcheck-exec-status-%s\n' "$cppcheck_status"
    cat "$out_dir/cppcheck.txt"
    exit "$cppcheck_status"
  fi
  if [ -s "$out_dir/cppcheck.txt" ]; then
    cppcheck_result=WARN_NONBLOCKING
  else
    cppcheck_result=PASS
  fi
else
  printf 'cppcheck not installed; skipped.\n' > "$out_dir/cppcheck.txt"
fi

printf 'QBOOT_HOST_STATIC_CHECK_PASS gcc-fanalyzer\n'
printf 'QBOOT_HOST_STATIC_CHECK_PASS cppcheck-%s\n' "$cppcheck_result"
printf '# QBoot Host Static Checks\n\n- gcc `-fanalyzer`: PASS (`%s`)\n- cppcheck: %s (`%s`)\n' \
  "$out_dir/gcc-fanalyzer.log" "$cppcheck_result" "$out_dir/cppcheck.txt" > "$summary"
