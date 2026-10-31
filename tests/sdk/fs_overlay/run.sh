#!/usr/bin/env bash
# Host test for the SDK_PORT_3DS ROM-archive and overlay hooks (patch 0603): the libpak
# registration in lib/NitroSDK/src/fs/FS_rom.c and the port calls in
# lib/NitroSDK/src/fs/FS_overlay.c (plus the overlay.h halves of patch 1402).
# Usage: tests/sdk/fs_overlay/run.sh [ROOT]   (patched decomp tree, default build/upstream;
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
OUT=$(mktemp -d "${TMPDIR:-/tmp}/fs_overlay_test.XXXXXX")
trap 'rm -rf "$OUT"' EXIT

cc() { "$GCC" -m32 -malign-double "${FLAGS[@]}" -c "$1" -o "$2"; }

cc "$ROOT/lib/NitroSDK/src/fs/FS_overlay.c" "$OUT/FS_overlay.o"
cc "$ROOT/lib/NitroSDK/src/fs/FS_rom.c" "$OUT/FS_rom.o"
cc "$HERE/../port_memory.c" "$OUT/port_memory.o"
cc "$HERE/test_fs_overlay.c" "$OUT/test_fs_overlay.o"
"$GCC" -m32 "$OUT/test_fs_overlay.o" "$OUT/port_memory.o" "$OUT/FS_overlay.o" "$OUT/FS_rom.o" \
    -o "$OUT/test_fs_overlay"

"$OUT/test_fs_overlay"
