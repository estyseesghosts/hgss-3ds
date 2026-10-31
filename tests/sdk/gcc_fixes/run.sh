#!/usr/bin/env bash
# Host checks for the GCC-only fixes in patches/pokeheartgold/1201 (all under SDK_PORT_3DS):
#   (a) lib/NitroSDK/src/snd/SND_command.c compiles and defines PxiFifoCallback (its unnamed parameter
#       was a GCC 10 error);
#   (b) nitro/math/crc.h and MATH_crc.h in one translation unit, with the table CRC through both;
#   (c) PXI_FIFO_TAG_CTRDG_PHI == 17 and PXI_MAX_FIFO_TAG == 32 under SDK_PORT_3DS;
#   (d) without SDK_PORT_3DS the PHI tag does not exist, so the DS build's enum is unchanged.
#
# Usage: tests/sdk/gcc_fixes/run.sh [ROOT]
#   ROOT is a patched decomp tree (default: build/upstream). Run it inside the dev container.
#   GCC defaults to gcc-10, the compiler the port's host32 build uses (the make log shows `gcc-10`);
#   the survey tool uses gcc 13, which accepts the unnamed parameter this patch removes.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
ROOT=$(cd "${1:-$REPO/build/upstream}" && pwd)
GCC=${GCC:-gcc-10}

# The survey tool's defines, CFLAGS and include order (imported, so the two cannot drift).
read -r -a FLAGS <<< "$(python3 -I -c '
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, sys.argv[1])
sys.dont_write_bytecode = True  # keep tools/__pycache__ out of the repo
import sdk_compile_survey as s
print(" ".join(s.DEFINES + s.CFLAGS + s.includes(sys.argv[2])))
' "$REPO/tools" "$ROOT")"
NOPORT_FLAGS=()
for f in "${FLAGS[@]}"; do
    [ "$f" = "-DSDK_PORT_3DS" ] || NOPORT_FLAGS+=("$f")
done

OUT=$(mktemp -d "${TMPDIR:-/tmp}/gcc_fixes_test.XXXXXX")
trap 'rm -rf "$OUT"' EXIT
CC_FLAGS=(-m32 -malign-double "${FLAGS[@]}")

fails=0
note() { printf '%s\n' "$*"; }
fail() { note "FAIL: $*"; fails=$((fails + 1)); }

# (a) SND_command.c compiles, and PxiFifoCallback is defined in it.
if "$GCC" "${CC_FLAGS[@]}" -c "$ROOT/lib/NitroSDK/src/snd/SND_command.c" -o "$OUT/snd.o" 2> "$OUT/snd.err"; then
    if nm "$OUT/snd.o" | grep -q ' T PxiFifoCallback$'; then
        note "ok   (a) SND_command.c compiles and defines PxiFifoCallback"
    else
        fail "(a) SND_command.c compiled but PxiFifoCallback is not defined in it"
    fi
else
    cat "$OUT/snd.err"
    fail "(a) SND_command.c does not compile"
fi

# (b) Both CRC headers in one TU, the table CRC through both, against a bit-by-bit reference.
"$GCC" "${CC_FLAGS[@]}" -c "$ROOT/lib/NitroSDK/src/math/MATH_crc.c" -o "$OUT/math_crc.o"
if "$GCC" "${CC_FLAGS[@]}" -c "$HERE/test_crc_headers.c" -o "$OUT/test_crc.o" \
   && "$GCC" -m32 "$OUT/test_crc.o" "$OUT/math_crc.o" -o "$OUT/test_crc" \
   && "$OUT/test_crc"; then
    note "ok   (b) CRC headers in one TU"
else
    fail "(b) CRC headers test"
fi

# (c) PXI tag values under the port's macro.
if "$GCC" "${CC_FLAGS[@]}" -c "$HERE/test_pxi_tags.c" -o "$OUT/test_pxi.o" \
   && "$GCC" -m32 "$OUT/test_pxi.o" -o "$OUT/test_pxi" \
   && "$OUT/test_pxi"; then
    note "ok   (c) PXI_FIFO_TAG_CTRDG_PHI == 17, PXI_MAX_FIFO_TAG == 32"
else
    fail "(c) PXI tag values"
fi

# (d) Without the macro, the test file must not compile, and the error must name the PHI tag.
if "$GCC" "${NOPORT_FLAGS[@]}" -m32 -malign-double -c "$HERE/test_pxi_tags.c" -o "$OUT/nop.o" 2> "$OUT/nop.err"; then
    fail "(d) PHI tag is defined without SDK_PORT_3DS"
elif grep -q 'PXI_FIFO_TAG_CTRDG_PHI' "$OUT/nop.err"; then
    note "ok   (d) without SDK_PORT_3DS the PHI tag is absent"
else
    cat "$OUT/nop.err"
    fail "(d) compile failed for a reason other than the missing PHI tag"
fi

if [ "$fails" -ne 0 ]; then
    note "gcc_fixes: $fails check group(s) failed"
    exit 1
fi
note "gcc_fixes: all check groups passed"
