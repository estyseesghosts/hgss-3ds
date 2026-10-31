#!/bin/sh
# Host check for the GCC CountLeadingZeros helpers that SDK_PORT_3DS adds to the three
# NitroSystem g2d sources (g2d_canvas_arrange.c, g2d_char_canvas.c, g2d_char_canvas_util.c).
#
# Usage: tests/sdk/nns_clz/run.sh <patched-root>
#   <patched-root> is a tree with the NitroSystem patch applied, e.g. /work/wt/s3.
# Each helper is extracted from its clang-format block, unchanged, and compiled with
# -m32 -DSDK_PORT_3DS by test_clz.c. Exits non-zero on any failure.
set -eu

if [ $# -ne 1 ]; then
    echo "usage: $0 <patched-root>" >&2
    exit 2
fi
ROOT=$1
HERE=$(cd "$(dirname "$0")" && pwd)
CC=${CC:-gcc}
WORK=$(mktemp -d "${TMPDIR:-/tmp}/nns_clz.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

fail=0
for f in g2d_canvas_arrange g2d_char_canvas g2d_char_canvas_util; do
    src="$ROOT/lib/NitroSystem/src/$f.c"
    if [ ! -f "$src" ]; then
        echo "FAIL $f: missing $src" >&2
        exit 1
    fi
    # The helper sits in exactly one clang-format block per file.
    awk -v out="$WORK/$f.inc" '
        /clang-format off/ { if (open) bad = 1; open = 1; blocks++ }
        open { print > out }
        /clang-format on/ { if (!open) bad = 1; open = 0 }
        END { if (blocks != 1 || bad || open) exit 1 }
    ' "$src" || { echo "FAIL $f: expected one clang-format block" >&2; exit 1; }

    # The MWCC asm must still be there, under #else.
    if ! grep -q 'asm { clz x, x }' "$WORK/$f.inc"; then
        echo "FAIL $f: MWCC asm clz block missing" >&2
        exit 1
    fi
    if ! grep -q '^#else' "$WORK/$f.inc" || ! grep -q '^#ifdef SDK_PORT_3DS' "$WORK/$f.inc"; then
        echo "FAIL $f: helper is not under SDK_PORT_3DS" >&2
        exit 1
    fi

    bin="$WORK/test_$f"
    if ! "$CC" -m32 -std=gnu99 -Wall -Werror -DSDK_PORT_3DS \
            -DCLZ_NAME="\"$f\"" -DCLZ_SNIPPET="\"$WORK/$f.inc\"" \
            "$HERE/test_clz.c" -o "$bin"; then
        echo "FAIL $f: does not compile" >&2
        fail=1
        continue
    fi
    if ! "$bin"; then
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "nns_clz: FAILED" >&2
    exit 1
fi
echo "nns_clz: all helpers ok"
