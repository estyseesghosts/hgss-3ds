#!/usr/bin/env bash
# Host test for the SDK_PORT_3DS C versions in lib/NitroSDK/src/os/os_system.c.
# Usage: tests/sdk/os_system/run.sh [ROOT]   (patched decomp tree, default build/upstream;
# run inside the dev container). Uses the survey tool's defines/flags/includes.
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
OUT=$(mktemp -d "${TMPDIR:-/tmp}/os_system_test.XXXXXX")
trap 'rm -rf "$OUT"' EXIT
"$GCC" -m32 -malign-double "${FLAGS[@]}" -c "$ROOT/lib/NitroSDK/src/os/os_system.c" -o "$OUT/os_system.o"
"$GCC" -m32 -malign-double "${FLAGS[@]}" -c "$HERE/test_os_system.c" -o "$OUT/test.o"
"$GCC" -m32 "$OUT/test.o" "$OUT/os_system.o" -o "$OUT/test"
"$OUT/test"
