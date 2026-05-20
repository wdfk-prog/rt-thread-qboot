#!/usr/bin/env bash
set -euo pipefail

required_results="${QBOOT_STATIC_REQUIRED_RESULTS:?missing required static results}"
hpatchlite_result="${QBOOT_STATIC_HPATCHLITE_RESULT:?missing hpatchlite static result}"
event_name="${QBOOT_GITHUB_EVENT_NAME:?missing GitHub event name}"

rm -rf _ci/static-merge _ci/host-sim/static-checks
mkdir -p _ci/static-merge _ci/host-sim/static-checks
summary="_ci/host-sim/static-checks/static_checks_summary.md"
printf '# QBoot Host Static Checks\n\n' > "$summary"

find _ci/artifacts/static -name '*.tar' -print | sort | while IFS= read -r archive; do
  case_dir="_ci/static-merge/$(basename "$archive" .tar)"
  mkdir -p "$case_dir"
  tar -xf "$archive" -C "$case_dir"
  find "$case_dir" -path '*/static_checks_summary.md' -print | sort | while IFS= read -r partial; do
    sed -n '/^- /p' "$partial" >> "$summary"
  done
  find "$case_dir" -path '*/static-checks/*' -type f ! -name static_checks_summary.md -exec cp -f '{}' _ci/host-sim/static-checks/ \;
done

for result in $required_results; do
  if [ "$result" != "success" ]; then
    printf 'mandatory static check result was %s\n' "$result" >&2
    exit 1
  fi
done

case "$hpatchlite_result" in
  success) ;;
  skipped)
    case "$event_name" in
      workflow_dispatch|schedule)
        echo "host-qboot-cppcheck-hpatchlite result was skipped" >&2
        exit 1
        ;;
      *)
        printf -- '- hpatchlite external cppcheck: SKIPPED (only runs on workflow_dispatch/schedule).\n' >> "$summary"
        ;;
    esac
    ;;
  cancelled)
    case "$event_name" in
      workflow_dispatch|schedule)
        echo "host-qboot-cppcheck-hpatchlite result was cancelled" >&2
        exit 1
        ;;
      *)
        printf -- '- hpatchlite external cppcheck: SKIPPED (only runs on workflow_dispatch/schedule).\n' >> "$summary"
        ;;
    esac
    ;;
  *)
    printf 'host-qboot-cppcheck-hpatchlite result was %s\n' "$hpatchlite_result" >&2
    exit 1
    ;;
esac

tar -cf _ci/qboot-host-static-checks.tar -C . _ci/host-sim/static-checks
