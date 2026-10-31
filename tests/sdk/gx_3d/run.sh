#!/usr/bin/env bash
# Host test for the SDK_PORT_3DS 3D command hooks (patch 0604): the G3_* immediate writes
# in lib/include/nitro/gx/g3imm.h, the matrix loads in lib/NitroSDK/src/gx/g3imm.c, the
# GXSTAT stack-error clears in lib/NitroSDK/src/gx/g3x.c, the G3X_ResetMtxStackOverflow
# inline in lib/include/nitro/gx/g3x.h, and the static MI_GetUncompressedSize in
# lib/include/nitro/mi/uncompress.h (header halves of patch 1402).
# Usage: tests/sdk/gx_3d/run.sh [ROOT]   (patched decomp tree, default build/upstream;
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
OUT=$(mktemp -d "${TMPDIR:-/tmp}/gx_3d_test.XXXXXX")
trap 'rm -rf "$OUT"' EXIT

cc() { "$GCC" -m32 -malign-double "${FLAGS[@]}" -c "$1" -o "$2"; }

cc "$ROOT/lib/NitroSDK/src/gx/g3imm.c" "$OUT/g3imm.o"
cc "$ROOT/lib/NitroSDK/src/gx/g3x.c" "$OUT/g3x.o"
cc "$HERE/../port_memory.c" "$OUT/port_memory.o"
cc "$HERE/test_gx_3d.c" "$OUT/test_gx_3d.o"
"$GCC" -m32 "$OUT/test_gx_3d.o" "$OUT/port_memory.o" "$OUT/g3imm.o" "$OUT/g3x.o" \
    -o "$OUT/test_gx_3d"

"$OUT/test_gx_3d"
