#!/usr/bin/env bash
set -euo pipefail

status=0
for path in "$@"; do
  if [ -f "$path" ] && grep -Iq . "$path" && grep -q $'\r' "$path"; then
    printf 'QBOOT_LINE_ENDING_FAIL %s contains CRLF line endings\n' "$path" >&2
    status=1
  fi
done
if [ "$status" -ne 0 ]; then
  printf 'Run git add --renormalize for the reported files.\n' >&2
  exit "$status"
fi
printf 'QBOOT_LINE_ENDING_PASS checked %d files\n' "$#"
