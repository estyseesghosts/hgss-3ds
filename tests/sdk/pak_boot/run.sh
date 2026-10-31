#!/usr/bin/env bash
# Boot-time data-pack check: compile hgss/game/app/heartgold.c's
# Heartgold_OpenPak with the port's real pak path —
#   NitroPort_OpenPak (3ds_port/nitro/src/port_fs.c)
#   pak_open/pak_find/pak_read (3ds_port/pak/src/pak.c)
# — and open synthetic packs (tests/sdk/pak_boot/make_packs.py).
#
# Usage: tests/sdk/pak_boot/run.sh [ROOT]   (patched decomp tree, default
# build/upstream; run inside the dev container). Exits non-zero if a build
# or a check fails.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
ROOT=$(cd "${1:-$REPO/build/upstream}" && pwd)
GCC=${GCC:-gcc-10}
command -v "$GCC" >/dev/null 2>&1 || GCC=cc
OUT=$(mktemp -d "${TMPDIR:-/tmp}/pak_boot_test.XXXXXX")
trap 'rm -rf "$OUT"' EXIT

# The harness's host32 flags and include paths (the same ones the game is
# compiled with), so this test exercises the real translation units.
read -r -a FLAGS <<< "$(python3 -I -c '
import sys
sys.path.insert(0, sys.argv[1])
from pathlib import Path
import hgss_game as hg
import hgss_nitro as nitro
root = Path(sys.argv[2]).resolve()
gen = Path(sys.argv[3]).resolve()
sdk, _ = nitro.make_shim(root)
print(" ".join(hg.port_cflags("host32")
               + hg.hgss_includes(root, sdk, gen)
               + ["-I" + sys.argv[4]]))
' "$REPO/tools" "$ROOT" "$OUT/gen" "$REPO/hgss/game/app")"

mkdir -p "$OUT/gen"
python3 "$HERE/make_packs.py" "$OUT/packs"

cc() { "$GCC" -m32 -malign-double "${FLAGS[@]}" "$@"; }
cc -c "$REPO/hgss/game/app/heartgold.c" -o "$OUT/heartgold.o"
cc -c "$REPO/3ds_port/nitro/src/port_fs.c" -o "$OUT/port_fs.o"
cc -c "$REPO/3ds_port/pak/src/pak.c" -o "$OUT/pak.o"
cc -c "$HERE/test_pak_boot.c" -o "$OUT/test_pak_boot.o"

"$GCC" -m32 -no-pie -Wl,--gc-sections \
    "$OUT/test_pak_boot.o" "$OUT/heartgold.o" "$OUT/port_fs.o" "$OUT/pak.o" \
    -o "$OUT/test_pak_boot"

"$OUT/test_pak_boot" "$OUT/packs"
