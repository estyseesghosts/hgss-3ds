#!/usr/bin/env bash
# Host test for the SDK_PORT_3DS C version of MATH_QSort (lib/NitroSDK/src/math/math_qsort.c).
# Usage: tests/sdk/math_qsort/run.sh [ROOT]   (patched NitroSDK tree, default build/upstream;
# run inside the dev container). The compile flags are the survey tool's (-m32 -malign-double,
# -DSDK_PORT_3DS). The test driver is built without the NitroSDK include path, so the system
# headers are used.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
ROOT=$(cd "${1:-$REPO/build/upstream}" && pwd)
GCC=${GCC:-gcc}
read -r -a FLAGS <<< "$(python3 -I -c '
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, sys.argv[1])
import sdk_compile_survey as s
print(" ".join(s.DEFINES + [f for f in s.CFLAGS if f != "-fgnu89-inline"] + s.includes(sys.argv[2])))
' "$REPO/tools" "$ROOT")"
OUT=$(mktemp -d "${TMPDIR:-/tmp}/math_qsort_test.XXXXXX")
trap 'rm -rf "$OUT"' EXIT
"$GCC" -m32 -malign-double "${FLAGS[@]}" -c "$ROOT/lib/NitroSDK/src/math/math_qsort.c" -o "$OUT/math_qsort.o"
"$GCC" -m32 -std=gnu99 -Wall -Wextra -Wno-unused-parameter -c "$HERE/test_math_qsort.c" -o "$OUT/test.o"
"$GCC" -m32 "$OUT/test.o" "$OUT/math_qsort.o" -o "$OUT/test"
"$OUT/test"
