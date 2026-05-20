#!/usr/bin/env bash
set -euo pipefail

while IFS= read -r profile || [ -n "$profile" ]; do
  profile=${profile%$'\r'}
  case "$profile" in
    ''|'#'*) continue ;;
  esac
  if [ ! -f ".github/ci/qboot/profiles/$profile.h" ]; then
    printf 'missing qboot profile fragment: %s\n' "$profile" >&2
    exit 1
  fi
done < .github/ci/qboot/profile-list.txt
