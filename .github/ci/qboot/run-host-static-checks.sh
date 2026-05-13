#!/usr/bin/env bash
set -euo pipefail

host_root="${QBOOT_HOST_OUT_DIR:-_ci/host-sim}"
out_dir="$host_root/static-checks"
status_dir="$out_dir/status"
summary="$out_dir/static_checks_summary.md"
board_parser_out_dir="$out_dir/board-smoke-parser"
selected_checks="${QBOOT_HOST_STATIC_CHECKS:-gcc-fanalyzer cppcheck board-parser}"
cppcheck_jobs="${QBOOT_HOST_CPPCHECK_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '2')}"
cppcheck_name="${QBOOT_HOST_CPPCHECK_NAME:-cppcheck}"
cppcheck_bin="${QBOOT_HOST_CPPCHECK_BIN:-cppcheck}"
cppcheck_paths="${QBOOT_HOST_CPPCHECK_PATHS:-src algorithm tests/host .github/ci/qboot/packages/hpatchlite}"
cppcheck_mode="${QBOOT_HOST_CPPCHECK_MODE:-deep}"

validate_cppcheck_config() {
  case "$cppcheck_jobs" in
    ''|*[!0-9]*)
      echo "invalid QBOOT_HOST_CPPCHECK_JOBS: $cppcheck_jobs" >&2
      exit 1
      ;;
  esac
  if [ "$cppcheck_jobs" -lt 1 ]; then
    echo "QBOOT_HOST_CPPCHECK_JOBS must be greater than zero" >&2
    exit 1
  fi
  case "$cppcheck_name" in
    .*)
      echo "QBOOT_HOST_CPPCHECK_NAME must not start with a dot: $cppcheck_name" >&2
      exit 1
      ;;
    gcc-fanalyzer|board-parser)
      echo "QBOOT_HOST_CPPCHECK_NAME must not reuse reserved check names: $cppcheck_name" >&2
      exit 1
      ;;
    ''|*[!A-Za-z0-9_.-]*)
      echo "invalid QBOOT_HOST_CPPCHECK_NAME: $cppcheck_name" >&2
      exit 1
      ;;
  esac
  if [ -z "$cppcheck_bin" ]; then
    echo "QBOOT_HOST_CPPCHECK_BIN must not be empty" >&2
    exit 1
  fi
  if [ -z "$cppcheck_paths" ]; then
    echo "QBOOT_HOST_CPPCHECK_PATHS must not be empty" >&2
    exit 1
  fi
  case "$cppcheck_mode" in
    fast|deep) ;;
    *)
      echo "invalid QBOOT_HOST_CPPCHECK_MODE: $cppcheck_mode" >&2
      exit 1
      ;;
  esac
}

mkdir -p "$out_dir" "$status_dir"
rm -f "$status_dir"/*.pid "$status_dir"/*.status "$status_dir"/*.result

check_selected() {
  local wanted=$1 item
  for item in $selected_checks; do
    if [ "$item" = "$wanted" ]; then
      return 0
    fi
  done
  return 1
}

validate_selected_checks() {
  local item selected_count=0 seen=""

  for item in $selected_checks; do
    selected_count=$((selected_count + 1))
    case "$item" in
      gcc-fanalyzer|cppcheck|board-parser) ;;
      *)
        echo "invalid QBOOT_HOST_STATIC_CHECKS entry: $item" >&2
        exit 1
        ;;
    esac
    case " $seen " in
      *" $item "*)
        echo "duplicate QBOOT_HOST_STATIC_CHECKS entry: $item" >&2
        exit 1
        ;;
    esac
    seen="$seen $item"
  done

  if [ "$selected_count" -eq 0 ]; then
    echo "QBOOT_HOST_STATIC_CHECKS must select at least one check" >&2
    exit 1
  fi
}

write_status() {
  local name=$1 status=$2 result=${3:-}
  printf '%s\n' "$status" > "$status_dir/$name.status"
  if [ -n "$result" ]; then
    printf '%s\n' "$result" > "$status_dir/$name.result"
  fi
}

run_gcc_fanalyzer() {
  local status=0
  if QBOOT_HOST_ANALYZER=1 QBOOT_HOST_BACKENDS="custom" \
    QBOOT_HOST_OUT_DIR="$host_root" bash .github/ci/qboot/build-host-sim.sh > "$out_dir/gcc-fanalyzer.log" 2>&1; then
    write_status gcc-fanalyzer 0 PASS
    return 0
  else
    status=$?
    write_status gcc-fanalyzer "$status" FAIL
    return "$status"
  fi
}

run_board_parser() {
  local status=0
  if QBOOT_BOARD_PARSER_OUT_DIR="$board_parser_out_dir" \
    bash .github/ci/qboot/test-board-smoke-parser.sh > "$out_dir/board-parser.log" 2>&1; then
    write_status board-parser 0 PASS
    return 0
  else
    status=$?
    write_status board-parser "$status" FAIL
    return "$status"
  fi
}

run_cppcheck() {
  local cppcheck_status=0 cppcheck_log="$out_dir/$cppcheck_name.txt"

  if ! command -v "$cppcheck_bin" >/dev/null 2>&1; then
    printf 'cppcheck binary `%s` not installed.\n' "$cppcheck_bin" > "$cppcheck_log"
    write_status "$cppcheck_name" 127 TOOL_MISSING
    return 127
  fi

  local -a cppcheck_flags cppcheck_path_args
  case "$cppcheck_mode" in
    deep)
      cppcheck_flags=(--enable=warning,style,performance,portability --inconclusive --force)
      ;;
    fast)
      cppcheck_flags=(--enable=warning,performance,portability)
      ;;
  esac

  read -r -a cppcheck_path_args <<< "$cppcheck_paths"
  "$cppcheck_bin" "${cppcheck_flags[@]}" \
    --std=c11 --quiet -j"$cppcheck_jobs" \
    --template=gcc \
    --suppress=missingInclude --suppress=missingIncludeSystem \
    -Iinc -Itests/host -Itests/host/stubs -I.github/ci/qboot/packages/hpatchlite \
    "${cppcheck_path_args[@]}" \
    2> "$cppcheck_log" || cppcheck_status=$?

  if grep -Eq '^[^:]+:[0-9]+:[0-9]+: error:' "$cppcheck_log"; then
    write_status "$cppcheck_name" 1 ERROR
    return 1
  fi
  if [ "$cppcheck_status" -ne 0 ]; then
    write_status "$cppcheck_name" "$cppcheck_status" "EXEC_STATUS_$cppcheck_status"
    return "$cppcheck_status"
  fi
  if [ -s "$cppcheck_log" ]; then
    write_status "$cppcheck_name" 0 WARN_NONBLOCKING
  else
    write_status "$cppcheck_name" 0 PASS
  fi
  return 0
}

start_static_check() {
  local name=$1
  shift
  "$@" &
  printf '%s\n' "$!" > "$status_dir/$name.pid"
}

read_check_status() {
  local name=$1 status_file
  status_file="$status_dir/$name.status"
  if [ ! -f "$status_file" ]; then
    printf 'missing'
    return 1
  fi
  if ! grep -Eq '^[0-9]+$' "$status_file"; then
    printf 'invalid'
    return 1
  fi
  cat "$status_file"
}

validate_selected_checks

if check_selected gcc-fanalyzer; then
  start_static_check gcc-fanalyzer run_gcc_fanalyzer
fi
if check_selected board-parser; then
  start_static_check board-parser run_board_parser
fi
if check_selected cppcheck; then
  validate_cppcheck_config
  start_static_check "$cppcheck_name" run_cppcheck
fi

wait_failed=0
if compgen -G "$status_dir/*.pid" >/dev/null; then
  for pid_file in "$status_dir"/*.pid; do
    pid=$(cat "$pid_file")
    if ! wait "$pid"; then
      wait_failed=1
    fi
  done
fi

{
  printf '# QBoot Host Static Checks\n\n'
  printf 'Selected checks: `%s`.\n\n' "$selected_checks"
} > "$summary"

if check_selected gcc-fanalyzer; then
  gcc_status=$(read_check_status gcc-fanalyzer || true)
  if [ "$gcc_status" != "0" ]; then
    printf 'QBOOT_HOST_STATIC_CHECK_FAIL gcc-fanalyzer\n'
    cat "$out_dir/gcc-fanalyzer.log"
    exit 1
  fi
  printf 'QBOOT_HOST_STATIC_CHECK_PASS gcc-fanalyzer\n'
  printf -- '- gcc `-fanalyzer`: PASS (`%s`)\n' "$out_dir/gcc-fanalyzer.log" >> "$summary"
fi

if check_selected cppcheck; then
  cppcheck_status=$(read_check_status "$cppcheck_name" || true)
  cppcheck_result=$(cat "$status_dir/$cppcheck_name.result" 2>/dev/null || printf 'UNKNOWN')
  cppcheck_log="$out_dir/$cppcheck_name.txt"
  if [ "$cppcheck_status" != "0" ]; then
    case "$cppcheck_result" in
      ERROR) printf 'QBOOT_HOST_STATIC_CHECK_FAIL %s-error\n' "$cppcheck_name" ;;
      EXEC_STATUS_*) printf 'QBOOT_HOST_STATIC_CHECK_FAIL %s-exec-status-%s\n' "$cppcheck_name" "${cppcheck_result#EXEC_STATUS_}" ;;
      *) printf 'QBOOT_HOST_STATIC_CHECK_FAIL %s\n' "$cppcheck_name" ;;
    esac
    cat "$cppcheck_log"
    exit 1
  fi
  printf 'QBOOT_HOST_STATIC_CHECK_PASS %s-%s\n' "$cppcheck_name" "$cppcheck_result"
  printf -- '- cppcheck `%s`: %s (`%s`, bin=`%s`, mode=%s, jobs=%s, paths=`%s`)\n' \
    "$cppcheck_name" "$cppcheck_result" "$cppcheck_log" "$cppcheck_bin" "$cppcheck_mode" "$cppcheck_jobs" "$cppcheck_paths" >> "$summary"
fi

if check_selected board-parser; then
  board_status=$(read_check_status board-parser || true)
  if [ "$board_status" != "0" ]; then
    printf 'QBOOT_HOST_STATIC_CHECK_FAIL board-parser\n'
    cat "$out_dir/board-parser.log"
    exit 1
  fi
  printf 'QBOOT_HOST_STATIC_CHECK_PASS board-parser\n'
  printf -- '- board command parser: PASS (`%s`, `%s`)\n' \
    "$out_dir/board-parser.log" "$board_parser_out_dir/board_parser_summary.md" >> "$summary"
fi

if [ "$wait_failed" -ne 0 ]; then
  printf 'QBOOT_HOST_STATIC_CHECK_FAIL worker-exit\n'
  exit 1
fi
