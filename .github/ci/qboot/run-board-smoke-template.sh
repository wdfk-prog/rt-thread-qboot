#!/usr/bin/env bash
set -euo pipefail

out_dir="_ci/board-smoke"
summary="$out_dir/board_smoke_summary.md"
board_log="$out_dir/board.log"
host_log="$out_dir/host_commands.log"
mkdir -p "$out_dir"

expect_flash=${QBOOT_BOARD_EXPECT_FLASH:-QBOOT_BOARD_FLASH_OK}
expect_jump=${QBOOT_BOARD_EXPECT_JUMP:-QBOOT_BOARD_JUMP_OK}
expect_reset=${QBOOT_BOARD_EXPECT_RESET:-QBOOT_BOARD_RESET_OK}

{
  printf '# QBoot Board Smoke Template\n\n'
  printf '| Check | Result | Marker |\n|---|---:|---|\n'
} > "$summary"

parse_board_command() {
  local command_line=$1
  local i=0 len=${#command_line} ch quote='' word='' in_word=0
  board_command_argv=()

  while [ "$i" -lt "$len" ]; do
    ch=${command_line:i:1}
    if [ -n "$quote" ]; then
      if [ "$ch" = "$quote" ]; then
        quote=''
        in_word=1
      elif [ "$quote" = '"' ] && [ "$ch" = '\' ]; then
        i=$((i + 1))
        if [ "$i" -ge "$len" ]; then
          word="$word\\"
        else
          word="$word${command_line:i:1}"
        fi
        in_word=1
      else
        word="$word$ch"
        in_word=1
      fi
    else
      case "$ch" in
        ' ' | $'\t')
          if [ "$in_word" -ne 0 ]; then
            board_command_argv+=("$word")
            word=''
            in_word=0
          fi
          ;;
        "'" | '"')
          quote=$ch
          in_word=1
          ;;
        '\')
          i=$((i + 1))
          if [ "$i" -ge "$len" ]; then
            printf 'QBOOT_BOARD_SMOKE_FAIL command has trailing escape\n' >&2
            return 1
          fi
          word="$word${command_line:i:1}"
          in_word=1
          ;;
        *)
          word="$word$ch"
          in_word=1
          ;;
      esac
    fi
    i=$((i + 1))
  done

  if [ -n "$quote" ]; then
    printf 'QBOOT_BOARD_SMOKE_FAIL command has unterminated quote\n' >&2
    return 1
  fi
  if [ "$in_word" -ne 0 ]; then
    board_command_argv+=("$word")
  fi
}

run_board_command() {
  local name=$1 command_line=$2 stdout_file=$3
  local -a board_command_argv=()

  if [ -z "$command_line" ]; then
    printf 'QBOOT_BOARD_SMOKE_FAIL %s command is empty\n' "$name" >&2
    exit 1
  fi

  # Parse a shell-like argv string without evaluating it as shell code. Quotes
  # and backslash escapes group arguments, but variables, command substitution,
  # redirects, and command separators are never interpreted by this script.
  if ! parse_board_command "$command_line" || [ "${#board_command_argv[@]}" -eq 0 ] || [ -z "${board_command_argv[0]}" ]; then
    printf 'QBOOT_BOARD_SMOKE_FAIL %s command has no executable\n' "$name" >&2
    exit 1
  fi

  "${board_command_argv[@]}" >> "$stdout_file" 2>> "$host_log"
}

if [ "${QBOOT_BOARD_SMOKE_DRY_RUN:-0}" = "1" ]; then
  : > "$host_log"
  printf '%s\n%s\n%s\n' "$expect_flash" "$expect_jump" "$expect_reset" > "$board_log"
else
  : "${QBOOT_BOARD_FLASH_CMD:?set QBOOT_BOARD_FLASH_CMD to flash the board}"
  : "${QBOOT_BOARD_RESET_CMD:?set QBOOT_BOARD_RESET_CMD to reset the board}"
  : > "$host_log"
  : > "$board_log"
  run_board_command flash "$QBOOT_BOARD_FLASH_CMD" "$host_log"
  run_board_command reset "$QBOOT_BOARD_RESET_CMD" "$host_log"
  if [ -n "${QBOOT_BOARD_LOG_CMD:-}" ]; then
    run_board_command log "$QBOOT_BOARD_LOG_CMD" "$board_log"
  elif [ -n "${QBOOT_BOARD_LOG_FILE:-}" ]; then
    cat -- "$QBOOT_BOARD_LOG_FILE" >> "$board_log"
  else
    printf 'QBOOT_BOARD_SMOKE_FAIL missing log source\n' >&2
    exit 1
  fi
fi

check_marker() {
  local name=$1 marker=$2
  if grep -Fq "$marker" "$board_log"; then
    printf 'QBOOT_BOARD_SMOKE_PASS %s\n' "$name"
    printf '| %s | PASS | `%s` |\n' "$name" "$marker" >> "$summary"
  else
    printf 'QBOOT_BOARD_SMOKE_FAIL %s missing marker %s\n' "$name" "$marker" >&2
    printf '| %s | FAIL | `%s` |\n' "$name" "$marker" >> "$summary"
    exit 1
  fi
}

check_marker flash "$expect_flash"
check_marker jump "$expect_jump"
check_marker reset "$expect_reset"
printf 'Board smoke template checks passed. Summary: %s\n' "$summary"
