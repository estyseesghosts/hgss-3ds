#!/usr/bin/env bash
# Host checks for the GCC-only fixes in patches/pokeheartgold/1201 and 1202 (all under SDK_PORT_3DS):
#   (a) lib/NitroSDK/src/snd/SND_command.c compiles and defines PxiFifoCallback (its unnamed parameter
#       was a GCC 10 error);
#   (b) nitro/math/crc.h and MATH_crc.h in one translation unit, with the table CRC through both;
#   (c) PXI_FIFO_TAG_CTRDG_PHI == 17 and PXI_MAX_FIFO_TAG == 32 under SDK_PORT_3DS;
#   (d) without SDK_PORT_3DS the PHI tag does not exist, so the DS build's enum is unchanged;
#   (e) nitro/mi/memory.h MIi_Cpu* prototypes match 3ds_port/nitro/src/mi_memory.c (void * params,
#       plus the Send declarations), and mismatch without the macro;
#   (f) nitro/pxi/fifo.h PXI_* prototypes match 3ds_port/nitro/src/pxi.c (int tags, plus
#       PXIFifoEmtpyCallback / PXI_SetFifoSendCallback), and mismatch without the macro;
#   (g) nitro/os/cache.h and OS_cache.h declare DC_StoreRange(const void *, u32) as in
#       3ds_port/nitro/src/port_os.c:315, and mismatch without the macro;
#   (h) nitro/spi/userInfo.h spells the NVRAM bits both ways (favoriteColor/favouriteColour,
#       input_favoriteColor/input_favouriteColour) with unchanged sizes and offsets;
#   (i) dgt.h (and math_dgt_internal.h, via MATH_dgt.c) declare the DGT functions as in
#       3ds_port/nitro/src/dgt.c, and mismatch without the macro.
#   (j) CARDiCommandArg offers dst (the port's spelling) through both card headers — the public
#       one the port uses and the flat one the SDK sources use — aliasing dest with unchanged
#       size and offsets, and dst is absent without the macro.
#   (k) struct DGTHash2Context offers both field sets (the port's ntrtwl names and the game's
#       HGSS names) with unchanged size, and the compat nitro/dgt/dgt.h forwards the flat
#       structs under the port's bare names; the ntrtwl fields are absent without the macro.
#
# Each (e)-(k) test repeats the port's signatures, so without the macro it fails the same way the
# port build did ("conflicting types", unknown type name, or no member); with the macro it
# compiles (and (h), (j) and (k) run).
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

# (e) MI prototypes match the port's mi_memory.c; without the macro they conflict.
if "$GCC" "${CC_FLAGS[@]}" -c "$HERE/test_mi_memory.c" -o "$OUT/test_mi.o" 2> "$OUT/e.err"; then
    note "ok   (e) MIi_Cpu* prototypes match mi_memory.c"
else
    cat "$OUT/e.err"
    fail "(e) MI prototypes test"
fi
if "$GCC" "${NOPORT_FLAGS[@]}" -m32 -malign-double -c "$HERE/test_mi_memory.c" -o "$OUT/noe.o" 2> "$OUT/noe.err"; then
    fail "(e) MI prototypes match without SDK_PORT_3DS"
elif grep -q "conflicting types.*MIi_Cpu\|'MIi_CpuSend" "$OUT/noe.err"; then
    note "ok   (e) without SDK_PORT_3DS the MI prototypes conflict"
else
    cat "$OUT/noe.err"
    fail "(e) MI negative check failed for another reason"
fi

# (f) PXI prototypes match the port's pxi.c; without the macro they conflict.
if "$GCC" "${CC_FLAGS[@]}" -c "$HERE/test_pxi.c" -o "$OUT/test_pxi_sig.o" 2> "$OUT/f.err"; then
    note "ok   (f) PXI prototypes match pxi.c"
else
    cat "$OUT/f.err"
    fail "(f) PXI prototypes test"
fi
if "$GCC" "${NOPORT_FLAGS[@]}" -m32 -malign-double -c "$HERE/test_pxi.c" -o "$OUT/nof.o" 2> "$OUT/nof.err"; then
    fail "(f) PXI prototypes match without SDK_PORT_3DS"
elif grep -q "conflicting types.*PXI_\|PXIFifoEmtpyCallback" "$OUT/nof.err"; then
    note "ok   (f) without SDK_PORT_3DS the PXI prototypes conflict"
else
    cat "$OUT/nof.err"
    fail "(f) PXI negative check failed for another reason"
fi

# (g) DC_StoreRange takes const void * in both cache headers; without the macro it conflicts.
if "$GCC" "${CC_FLAGS[@]}" -c "$HERE/test_cache.c" -o "$OUT/test_cache.o" 2> "$OUT/g.err"; then
    note "ok   (g) DC_StoreRange takes const void *"
else
    cat "$OUT/g.err"
    fail "(g) cache prototypes test"
fi
if "$GCC" "${NOPORT_FLAGS[@]}" -m32 -malign-double -c "$HERE/test_cache.c" -o "$OUT/nog.o" 2> "$OUT/nog.err"; then
    fail "(g) DC_StoreRange matches without SDK_PORT_3DS"
elif grep -q "conflicting types.*DC_StoreRange" "$OUT/nog.err"; then
    note "ok   (g) without SDK_PORT_3DS DC_StoreRange conflicts"
else
    cat "$OUT/nog.err"
    fail "(g) cache negative check failed for another reason"
fi

# (h) Both NVRAM spellings work with unchanged layout; without the macro the US spelling is absent.
if "$GCC" "${CC_FLAGS[@]}" -c "$HERE/test_userinfo.c" -o "$OUT/test_userinfo.o" \
    && "$GCC" -m32 "$OUT/test_userinfo.o" -o "$OUT/test_userinfo" \
    && "$OUT/test_userinfo"; then
    note "ok   (h) NVRAM spellings alias with unchanged layout"
else
    fail "(h) userinfo test"
fi
if "$GCC" "${NOPORT_FLAGS[@]}" -m32 -malign-double -c "$HERE/test_userinfo.c" -o "$OUT/noh.o" 2> "$OUT/noh.err"; then
    fail "(h) favoriteColor exists without SDK_PORT_3DS"
elif grep -q 'favoriteColor' "$OUT/noh.err"; then
    note "ok   (h) without SDK_PORT_3DS the US spelling is absent"
else
    cat "$OUT/noh.err"
    fail "(h) userinfo negative check failed for another reason"
fi

# (i) DGT prototypes match the port's dgt.c (MATH_dgt.c covers math_dgt_internal.h too).
"$GCC" "${CC_FLAGS[@]}" -c "$ROOT/lib/NitroSDK/src/math/MATH_dgt.c" -o "$OUT/math_dgt.o"
if "$GCC" "${CC_FLAGS[@]}" -c "$HERE/test_dgt.c" -o "$OUT/test_dgt.o" 2> "$OUT/i.err"; then
    note "ok   (i) DGT prototypes match dgt.c"
else
    cat "$OUT/i.err"
    fail "(i) DGT prototypes test"
fi
if "$GCC" "${NOPORT_FLAGS[@]}" -m32 -malign-double -c "$HERE/test_dgt.c" -o "$OUT/noi.o" 2> "$OUT/noi.err"; then
    fail "(i) DGT prototypes match without SDK_PORT_3DS"
elif grep -q "conflicting types.*DGT_" "$OUT/noi.err"; then
    note "ok   (i) without SDK_PORT_3DS the DGT prototypes conflict"
else
    cat "$OUT/noi.err"
    fail "(i) DGT negative check failed for another reason"
fi

# (j) dst aliases dest through both card headers; without the macro it is absent.
for t in card card_flat; do
    if "$GCC" "${CC_FLAGS[@]}" -c "$HERE/test_$t.c" -o "$OUT/test_$t.o" \
        && "$GCC" -m32 "$OUT/test_$t.o" -o "$OUT/test_$t" \
        && "$OUT/test_$t"; then
        note "ok   (j) CARDiCommandArg.dst aliases dest ($t)"
    else
        fail "(j) card dst test ($t)"
    fi
    if "$GCC" "${NOPORT_FLAGS[@]}" -m32 -malign-double -c "$HERE/test_$t.c" -o "$OUT/noj_$t.o" 2> "$OUT/noj_$t.err"; then
        fail "(j) dst exists without SDK_PORT_3DS ($t)"
    elif grep -q 'dst' "$OUT/noj_$t.err"; then
        note "ok   (j) without SDK_PORT_3DS dst is absent ($t)"
    else
        cat "$OUT/noj_$t.err"
        fail "(j) card negative check failed for another reason ($t)"
    fi
done

# (k) Both DGTHash2Context field sets alias with unchanged size, and the compat
# header forwards the flat structs; without the macro the ntrtwl fields are absent.
if "$GCC" "${CC_FLAGS[@]}" -c "$HERE/test_dgt_struct.c" -o "$OUT/test_dgt_struct.o" \
    && "$GCC" -m32 "$OUT/test_dgt_struct.o" -o "$OUT/test_dgt_struct" \
    && "$OUT/test_dgt_struct"; then
    note "ok   (k) DGTHash2Context field sets alias with unchanged size"
else
    fail "(k) dgt struct test"
fi
if "$GCC" "${NOPORT_FLAGS[@]}" -m32 -malign-double -c "$HERE/test_dgt_struct.c" -o "$OUT/nok.o" 2> "$OUT/nok.err"; then
    fail "(k) ntrtwl fields exist without SDK_PORT_3DS"
elif grep -q 'h0' "$OUT/nok.err"; then
    note "ok   (k) without SDK_PORT_3DS the ntrtwl fields are absent"
else
    cat "$OUT/nok.err"
    fail "(k) dgt struct negative check failed for another reason"
fi
if "$GCC" -I"$REPO/hgss/compat/include" "${CC_FLAGS[@]}" -c "$HERE/test_dgt_compat.c" -o "$OUT/test_dgt_compat.o" 2> "$OUT/kc.err"; then
    note "ok   (k) compat nitro/dgt/dgt.h forwards the flat structs"
else
    cat "$OUT/kc.err"
    fail "(k) compat dgt header test"
fi

if [ "$fails" -ne 0 ]; then
    note "gcc_fixes: $fails check group(s) failed"
    exit 1
fi
note "gcc_fixes: all check groups passed"
