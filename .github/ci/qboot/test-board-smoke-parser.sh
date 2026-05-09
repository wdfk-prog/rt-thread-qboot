#!/usr/bin/env bash
set -euo pipefail

out_dir="${QBOOT_BOARD_PARSER_OUT_DIR:-_ci/board-smoke-parser}"
markers="$out_dir/markers.log"
mkdir -p "$out_dir"
printf 'QBOOT_BOARD_FLASH_OK\nQBOOT_BOARD_JUMP_OK\nQBOOT_BOARD_RESET_OK\n' > "$markers"

run_pass() {
  local name=$1 flash_cmd=$2 reset_cmd=$3
  QBOOT_BOARD_FLASH_CMD="$flash_cmd" \
  QBOOT_BOARD_RESET_CMD="$reset_cmd" \
  QBOOT_BOARD_LOG_FILE="$markers" \
    bash .github/ci/qboot/run-board-smoke-template.sh > "$out_dir/$name.log" 2>&1
  printf 'QBOOT_BOARD_PARSER_PASS %s\n' "$name"
}

run_fail() {
  local name=$1 flash_cmd=$2 reset_cmd=$3
  if QBOOT_BOARD_FLASH_CMD="$flash_cmd" \
     QBOOT_BOARD_RESET_CMD="$reset_cmd" \
     QBOOT_BOARD_LOG_FILE="$markers" \
       bash .github/ci/qboot/run-board-smoke-template.sh > "$out_dir/$name.log" 2>&1; then
    printf 'QBOOT_BOARD_PARSER_FAIL %s unexpected-pass\n' "$name" >&2
    cat "$out_dir/$name.log" >&2
    exit 1
  fi
  printf 'QBOOT_BOARD_PARSER_PASS %s\n' "$name"
}

argv_probe="$out_dir/argv-probe.sh"
argv_log="$out_dir/argv.log"
cat > "$argv_probe" <<'SH'
#!/usr/bin/env sh
set -eu

out_file=$1
case_name=$2
shift 2

{
  printf '%s argc=%s\n' "$case_name" "$#"
  i=1
  for arg do
    printf '%s argv%s=<%s>\n' "$case_name" "$i" "$arg"
    i=$((i + 1))
  done
} >> "$out_file"
SH
chmod +x "$argv_probe"

rm -f "$argv_log"
run_pass quoted-arguments \
  "\"$argv_probe\" \"$argv_log\" flash \"flash arg with spaces\" plain" \
  "\"$argv_probe\" \"$argv_log\" reset reset\\ arg"

if ! grep -Fx 'flash argc=2' "$argv_log" >/dev/null || \
   ! grep -Fx 'flash argv1=<flash arg with spaces>' "$argv_log" >/dev/null || \
   ! grep -Fx 'flash argv2=<plain>' "$argv_log" >/dev/null || \
   ! grep -Fx 'reset argc=1' "$argv_log" >/dev/null || \
   ! grep -Fx 'reset argv1=<reset arg>' "$argv_log" >/dev/null; then
  printf 'QBOOT_BOARD_PARSER_FAIL quoted-arguments argv mismatch\n' >&2
  cat "$argv_log" >&2
  exit 1
fi

eval_marker="$out_dir/eval-ran"
rm -f "$eval_marker"
run_pass literal-command-substitution "/bin/echo \$(touch \"$eval_marker\")" '/bin/echo reset'
if [ -e "$eval_marker" ]; then
  printf 'QBOOT_BOARD_PARSER_FAIL literal-command-substitution evaluated command substitution\n' >&2
  exit 1
fi
run_fail trailing-escape '/bin/echo trailing\' '/bin/echo reset'
run_fail unterminated-quote '/bin/echo "unterminated' '/bin/echo reset'

printf '# QBoot Board Command Parser Tests\n\nAll parser portability tests passed.\n' > "$out_dir/board_parser_summary.md"
