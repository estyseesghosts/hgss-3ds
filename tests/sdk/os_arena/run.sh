#!/usr/bin/env bash
# Host test for the SDK_PORT_3DS arena/stack defaults (patch 0302): the linker-symbol
# replacements in lib/NitroSDK/src/os/{os_arena.c,os_thread.c,os_interrupt.c}.
# Usage: tests/sdk/os_arena/run.sh [ROOT]   (patched decomp tree, default build/upstream;
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
OUT=$(mktemp -d "${TMPDIR:-/tmp}/os_arena_test.XXXXXX")
trap 'rm -rf "$OUT"' EXIT

cc() { "$GCC" -m32 -malign-double "${FLAGS[@]}" -c "$1" -o "$2"; }

cc "$ROOT/lib/NitroSDK/src/os/os_arena.c" "$OUT/os_arena.o"
cc "$ROOT/lib/NitroSDK/src/os/os_thread.c" "$OUT/os_thread.o"
cc "$HERE/../port_memory.c" "$OUT/port_memory.o"
cc "$HERE/test_os_arena.c" "$OUT/test_os_arena.o"
"$GCC" -m32 "$OUT/test_os_arena.o" "$OUT/port_memory.o" "$OUT/os_arena.o" "$OUT/os_thread.o" \
    -o "$OUT/test_os_arena"

"$OUT/test_os_arena"
