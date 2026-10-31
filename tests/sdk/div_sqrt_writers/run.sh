#!/usr/bin/env bash
# Host test for the remaining DIV/SQRT writer hooks (patch 0502): the raw divider and
# square-root register writes in lib/NitroSDK/src/fx/{fx_vec.c,fx_vec_mult_add.c,
# fx_vec_normalize.c,fx_mtx44_ortho.c,fx_mtx44_perspective.c} and lib/NitroSDK/src/tp.c.
#
# Usage: tests/sdk/div_sqrt_writers/run.sh [ROOT]   (patched decomp tree, default
# build/upstream; run inside the dev container). Uses the survey tool's defines/flags/includes.
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
OUT=$(mktemp -d "${TMPDIR:-/tmp}/div_sqrt_writers_test.XXXXXX")
trap 'rm -rf "$OUT"' EXIT

cc() { "$GCC" -m32 -malign-double "${FLAGS[@]}" -c "$1" -o "$2"; }

cc "$ROOT/lib/NitroSDK/src/fx/fx_vec.c" "$OUT/fx_vec.o"
cc "$ROOT/lib/NitroSDK/src/fx/fx_vec_mult_add.c" "$OUT/fx_vec_mult_add.o"
cc "$ROOT/lib/NitroSDK/src/fx/fx_vec_normalize.c" "$OUT/fx_vec_normalize.o"
cc "$ROOT/lib/NitroSDK/src/fx/fx_mtx44_ortho.c" "$OUT/fx_mtx44_ortho.o"
cc "$ROOT/lib/NitroSDK/src/fx/fx_mtx44_perspective.c" "$OUT/fx_mtx44_perspective.o"
cc "$ROOT/lib/NitroSDK/src/fx/fx_cp.c" "$OUT/fx_cp.o"
cc "$ROOT/lib/NitroSDK/src/tp.c" "$OUT/tp.o"
cc "$HERE/../port_memory.c" "$OUT/port_memory.o"
cc "$HERE/test_div_sqrt.c" "$OUT/test_div_sqrt.o"
"$GCC" -m32 "$OUT/test_div_sqrt.o" "$OUT/port_memory.o" "$OUT/fx_vec.o" "$OUT/fx_vec_mult_add.o" \
    "$OUT/fx_vec_normalize.o" "$OUT/fx_mtx44_ortho.o" "$OUT/fx_mtx44_perspective.o" "$OUT/fx_cp.o" \
    "$OUT/tp.o" -o "$OUT/test_div_sqrt"

"$OUT/test_div_sqrt"
