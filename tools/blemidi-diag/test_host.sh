#!/usr/bin/env bash
# Host checks for the MothDiag parser, the stats meter, and diag.py.
# No board and no SDK.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CXX="${CXX:-g++}"
"$CXX" -std=c++17 -Wall -Wextra -Werror \
  -I"$ROOT/tools/blemidi-diag/include" \
  -I"$ROOT/tools/blemidi-diag/src" \
  -I"$ROOT/include" \
  "$ROOT/tools/blemidi-diag/test/test_main.cpp" \
  "$ROOT/tools/blemidi-diag/src/diag_parse.cpp" \
  "$ROOT/tools/blemidi-diag/src/diag_stats.cpp" \
  "$ROOT/tools/blemidi-diag/src/diag_selftest.cpp" \
  "$ROOT/src/MidiProtocol.cpp" \
  "$ROOT/src/BleAdvert.cpp" \
  -o /tmp/mothdiag_test
/tmp/mothdiag_test
python3 "$ROOT/tools/blemidi-diag/diag.py" --self-check
