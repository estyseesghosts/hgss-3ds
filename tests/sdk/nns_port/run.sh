#!/usr/bin/env bash
# Host test for the SDK_PORT_3DS NitroSystem hooks (patch 0302): the GXFIFO word in
# lib/NitroSystem/src/g3d_gecom.c, the early 3D transfers in
# lib/NitroSystem/src/gfd_vram_transfer_manager.c, and the hidden park entries in
# lib/NitroSystem/src/g2d_oam.c (plus the transfer-manager header half of patch 1402).
# Usage: tests/sdk/nns_port/run.sh [ROOT]   (patched decomp tree, default build/upstream;
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
OUT=$(mktemp -d "${TMPDIR:-/tmp}/nns_port_test.XXXXXX")
trap 'rm -rf "$OUT"' EXIT

cc() { "$GCC" -m32 -malign-double "${FLAGS[@]}" -c "$1" -o "$2"; }

cc "$ROOT/lib/NitroSystem/src/g3d_gecom.c" "$OUT/g3d_gecom.o"
cc "$ROOT/lib/NitroSystem/src/gfd_vram_transfer_manager.c" "$OUT/gfd_vram_transfer_manager.o"
cc "$ROOT/lib/NitroSystem/src/g2d_oam.c" "$OUT/g2d_oam.o"
cc "$HERE/../port_memory.c" "$OUT/port_memory.o"
cc "$HERE/test_nns_port.c" "$OUT/test_nns_port.o"
"$GCC" -m32 "$OUT/test_nns_port.o" "$OUT/port_memory.o" "$OUT/g3d_gecom.o" \
    "$OUT/gfd_vram_transfer_manager.o" "$OUT/g2d_oam.o" -o "$OUT/test_nns_port"

"$OUT/test_nns_port"
