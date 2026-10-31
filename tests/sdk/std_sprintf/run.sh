#!/bin/sh
# Host regression for the SDK_PORT_3DS va_list reads in std_sprintf.c (patch 1701).
#
# On the DS, MWCC's va_list is a byte pointer and STD_TVSNPrintf walks it with
# `va += 4; *(int *)(va - 4)`. devkitARM's newlib makes that fail to compile, so
# the patch reads each argument with va_arg and casts the promoted value to the
# width the format asks for. This suite compiles the patched formatter with the
# host32 flags (-m32 -malign-double, the same ABI as the DS for 64-bit slots)
# and checks the output of every read path, including the %n write.
#
# Usage: tests/sdk/std_sprintf/run.sh <patched-root>
#   <patched-root> is a tree with the pokeheartgold patch stack applied, e.g.
#   build/upstream. Synthetic arguments only. Exits non-zero on any failure.
set -eu

if [ $# -ne 1 ]; then
    echo "usage: $0 <patched-root>" >&2
    exit 2
fi
ROOT=$1
HERE=$(cd "$(dirname "$0")" && pwd)
CC=${CC:-gcc}
WORK=$(mktemp -d "${TMPDIR:-/tmp}/sdk_std_sprintf.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

if [ ! -f "$ROOT/lib/NitroSDK/src/std_sprintf.c" ]; then
    echo "FAIL: $ROOT is not a patched NitroSDK tree" >&2
    exit 1
fi

# The survey's flags. The decomp's lib/include is added with -idirafter, so it
# supplies nitro/*.h (hw/mmap.h, fx/fx_const.h) but cannot shadow the system
# <stdarg.h> the way an -I would (MSL's va_list is char*, not newlib's struct).
CFLAGS="-std=gnu99 -fno-strict-aliasing -fno-short-enums -fsigned-char -fwrapv \
-fno-aggressive-loop-optimizations -fno-delete-null-pointer-checks -Wall -Wno-unused-function \
-Wno-unused-variable -Wno-missing-braces -Wno-unknown-pragmas -Wno-unused-but-set-variable \
-Wno-parentheses -Wno-switch -Wno-multichar"
DEFS="-DSDK_ARM9 -DSDK_CODE_ARM -DSDK_FINALROM -DNNS_FINALROM -DSDK_PORT_3DS"
if [ ! -f "$ROOT/lib/include/nitro/fx/fx_const.h" ]; then
    echo "FAIL: $ROOT has no generated lib/include/nitro/fx/fx_const.h (run tools/bootstrap.py)" >&2
    exit 1
fi
INC="-I$ROOT/src -I$ROOT/include -I$ROOT/include/library \
-I$ROOT/lib/NitroSDK/include -I$ROOT/lib/NitroSDK/include-mw -I$ROOT/lib/NitroSDK/include-pokediamond \
-idirafter $ROOT/lib/include"
M32="-m32 -malign-double"

for f in std_sprintf std_sprintf_writer; do
    if ! "$CC" -c $M32 $CFLAGS $DEFS $INC "$ROOT/lib/NitroSDK/src/$f.c" -o "$WORK/$f.o"; then
        echo "FAIL $f: does not compile" >&2
        exit 1
    fi
done

if ! "$CC" -c $M32 -std=gnu99 -Wall -Werror "$HERE/test_std_sprintf.c" -o "$WORK/test_std_sprintf.o"; then
    echo "sdk std_sprintf: test driver does not compile" >&2
    exit 1
fi
if ! "$CC" $M32 "$WORK/test_std_sprintf.o" "$WORK/std_sprintf.o" "$WORK/std_sprintf_writer.o" \
        -o "$WORK/test_std_sprintf"; then
    echo "sdk std_sprintf: link failed" >&2
    exit 1
fi
if ! "$WORK/test_std_sprintf"; then
    exit 1
fi
echo "sdk std_sprintf: all ok"
