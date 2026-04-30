#!/usr/bin/env bash
set -euo pipefail

profile="${1:?missing qboot CI profile}"

if [ ! -d _ci/rt-thread-base ]; then
  bash .github/ci/qboot/prepare-stm32f407-base.sh
fi

bash .github/ci/qboot/build-stm32f407-profile.sh "$profile"
