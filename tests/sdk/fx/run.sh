#!/usr/bin/env bash
# Host test for the GCC (SDK_PORT_3DS) C versions of the MWCC matrix asm functions.
#
# Usage: tests/sdk/fx/run.sh [ROOT]
#   ROOT is a patched decomp tree (default: build/upstream). Run it inside the dev container.
#
# Compiles the eight fx_mtx*.c files under ROOT/lib/NitroSDK/src/fx and test_fx_matrix.c with
# gcc -m32 -malign-double, and the defines, CFLAGS and include order of tools/sdk_compile_survey.py
# (imported from that script, so the two cannot drift). Links and runs the test; exits non-zero
# if the build fails or any case fails.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
ROOT=$(cd "${1:-$REPO/build/upstream}" && pwd)
GCC=${GCC:-gcc}

read -r -a FLAGS <<< "$(python3 -I -c '
import sys
sys.dont_write_bytecode = True  # keep tools/__pycache__ out of the repo
sys.path.insert(0, sys.argv[1])
import sdk_compile_survey as s
print(" ".join(s.DEFINES + s.CFLAGS + s.includes(sys.argv[2])))
' "$REPO/tools" "$ROOT")"

FX_FILES=(fx_mtx22.c fx_mtx33_identity.c fx_mtx33_rot_yz.c fx_mtx43_identity.c
          fx_mtx43_rot.c fx_mtx43_scale.c fx_mtx44_identity.c fx_mtx44_rot.c)

OUT=$(mktemp -d "${TMPDIR:-/tmp}/fx_matrix_test.XXXXXX")
trap 'rm -rf "$OUT"' EXIT

OBJS=()
for f in "${FX_FILES[@]}"; do
    src="$ROOT/lib/NitroSDK/src/fx/$f"
    obj="$OUT/${f%.c}.o"
    "$GCC" -m32 -malign-double "${FLAGS[@]}" -c "$src" -o "$obj"
    OBJS+=("$obj")
done

"$GCC" -m32 -malign-double "${FLAGS[@]}" -c "$HERE/test_fx_matrix.c" -o "$OUT/test_fx_matrix.o"
"$GCC" -m32 -malign-double "$OUT/test_fx_matrix.o" "${OBJS[@]}" -o "$OUT/test_fx_matrix"

"$OUT/test_fx_matrix"
