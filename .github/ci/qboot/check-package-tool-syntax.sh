#!/usr/bin/env bash
set -euo pipefail

python3 -S -m py_compile \
  tools/package_tool.py \
  docs/package-tool/package_tool_web.py \
  tests/test_package_tool.py \
  tests/test_package_tool_web.py \
  tests/package_tool_web_static_checks.py \
  tests/package_tool_web_test_lib.py
