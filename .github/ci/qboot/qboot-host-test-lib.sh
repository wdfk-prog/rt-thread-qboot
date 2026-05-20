#!/usr/bin/env bash
# Shared host-test case selection helpers. Runners source this file so the
# blocking and current-policy CI jobs classify cases with the same rules.

qboot_case_scope=${QBOOT_CASE_SCOPE:-blocking}

qboot_case_scope_validate() {
  case "$qboot_case_scope" in
    blocking|current-policy|all) return 0 ;;
    *)
      printf 'unsupported QBOOT_CASE_SCOPE: %s\n' "$qboot_case_scope" >&2
      exit 2
      ;;
  esac
}

qboot_case_scope_validate

qboot_case_is_current_policy() {
  case "$1" in
    *current-policy*) return 0 ;;
    *) return 1 ;;
  esac
}

qboot_case_should_run() {
  case "$qboot_case_scope" in
    blocking)
      if qboot_case_is_current_policy "$1"; then
        return 1
      fi
      return 0
      ;;
    current-policy)
      qboot_case_is_current_policy "$1"
      ;;
    all)
      return 0
      ;;
    *)
      printf 'unsupported QBOOT_CASE_SCOPE: %s\n' "$qboot_case_scope" >&2
      exit 2
      ;;
  esac
}

qboot_case_scope_title() {
  case "$qboot_case_scope" in
    blocking) printf 'Blocking invariant' ;;
    current-policy) printf 'Current-policy informational' ;;
    all) printf 'All' ;;
    *) printf 'Unknown' ;;
  esac
}
