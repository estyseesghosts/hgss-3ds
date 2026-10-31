#!/usr/bin/env bash
# Host test for the SDK_PORT_3DS memory map in lib/include/nitro/hw/ARM9/mmap.h.
# Usage: tests/sdk/memory_map/run.sh [ROOT]   (patched decomp tree, default build/upstream;
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
OUT=$(mktemp -d "${TMPDIR:-/tmp}/memory_map_test.XXXXXX")
trap 'rm -rf "$OUT"' EXIT
"$GCC" -m32 -malign-double "${FLAGS[@]}" -c "$HERE/test_memory_map.c" -o "$OUT/test.o"
"$GCC" -m32 "$OUT/test.o" -o "$OUT/test"
"$OUT/test"
