#!/usr/bin/env bash
# Host tests for the SDK_PORT_3DS C versions in lib/NitroSDK/src/os/os_context.c and
# lib/NitroSDK/src/cp/CP_context.c, and the divider/sqrt hooks in lib/NitroSDK/src/fx/fx_cp.c.
# Usage: tests/sdk/cp_context/run.sh [ROOT]   (patched decomp tree, default build/upstream;
# run inside the dev container). Uses the survey tool's defines/flags/includes.
# Exits non-zero if a build fails or any check fails.
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
OUT=$(mktemp -d "${TMPDIR:-/tmp}/cp_context_test.XXXXXX")
trap 'rm -rf "$OUT"' EXIT

cc() { "$GCC" -m32 -malign-double "${FLAGS[@]}" -c "$1" -o "$2"; }

cc "$ROOT/lib/NitroSDK/src/cp/CP_context.c" "$OUT/CP_context.o"
cc "$ROOT/lib/NitroSDK/src/os/os_context.c" "$OUT/os_context.o"
cc "$HERE/../port_memory.c" "$OUT/port_memory.o"
cc "$HERE/test_context.c" "$OUT/test_context.o"
"$GCC" -m32 "$OUT/test_context.o" "$OUT/port_memory.o" "$OUT/CP_context.o" "$OUT/os_context.o" \
    -o "$OUT/test_context"
"$OUT/test_context"

cc "$ROOT/lib/NitroSDK/src/fx/fx_cp.c" "$OUT/fx_cp.o"
cc "$HERE/test_fx_cp.c" "$OUT/test_fx_cp.o"
"$GCC" -m32 "$OUT/test_fx_cp.o" "$OUT/port_memory.o" "$OUT/fx_cp.o" -o "$OUT/test_fx_cp"
"$OUT/test_fx_cp"
