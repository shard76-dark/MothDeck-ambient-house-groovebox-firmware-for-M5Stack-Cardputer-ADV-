#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CXX="${CXX:-g++}"
"$CXX" -std=c++17 -Wall -Wextra -Werror \
  -I"$ROOT/include" \
  -I"$ROOT/tools/mothboot/include" \
  "$ROOT/tools/mothboot/test_host.cpp" \
  "$ROOT/src/MothCrc.cpp" \
  "$ROOT/src/SdPattern.cpp" \
  "$ROOT/src/FatPlan.cpp" \
  -o /tmp/mothboot_test
/tmp/mothboot_test
