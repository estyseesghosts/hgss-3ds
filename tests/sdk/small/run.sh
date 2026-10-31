#!/bin/sh
# Host checks for the SDK_PORT_3DS C versions of small NitroSDK asm functions (patch 0201):
#   os/os_vram_exclusive_clz.c, os/os_spin_lock_id.c, os/os_terminate_proc.c, os/os_init.c,
#   gx/gxasm.c, gx/g3x_nop_clear_fifo.c   -> linked into test_small and run
#   rtc/rtc_api.c, mic.c                  -> compile-only (their busy waits poll ARM7 lock words)
#
# Usage: tests/sdk/small/run.sh <patched-root>
#   <patched-root> is a tree with the NitroSDK patch applied, e.g. /work/wt/s2.
# The compile flags are those of tools/sdk_compile_survey.py, built -m32 -malign-double, with
# -DSDK_PORT_3DS. Synthetic data only. Exits non-zero on any failure.
set -eu

if [ $# -ne 1 ]; then
    echo "usage: $0 <patched-root>" >&2
    exit 2
fi
ROOT=$1
HERE=$(cd "$(dirname "$0")" && pwd)
CC=${CC:-gcc}
WORK=$(mktemp -d "${TMPDIR:-/tmp}/sdk_small.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

if [ ! -f "$ROOT/lib/NitroSDK/src/os/os_spin_lock_id.c" ]; then
    echo "FAIL: $ROOT is not a patched NitroSDK tree" >&2
    exit 1
fi

# The survey's flags, except -fgnu89-inline: under it the header's `inline MI_GetUncompressedSize`
# (lib/include/nitro/mi/uncompress.h) is emitted in every object and the link fails. None of the
# functions tested here use an inline function, so C99 inline semantics change nothing tested.
CFLAGS="-std=gnu99 -fno-strict-aliasing -fno-short-enums -fsigned-char -fwrapv \
-fno-aggressive-loop-optimizations -fno-delete-null-pointer-checks -Wall -Wno-unused-function \
-Wno-unused-variable -Wno-missing-braces -Wno-unknown-pragmas -Wno-unused-but-set-variable \
-Wno-parentheses -Wno-switch -Wno-multichar"
DEFS="-DSDK_ARM9 -DSDK_CODE_ARM -DSDK_FINALROM -DNNS_FINALROM -DSDK_PORT_3DS"
INC="-I$ROOT/src -I$ROOT/include -I$ROOT/include/library -I$ROOT/lib/include \
-I$ROOT/lib/NitroSDK/include -I$ROOT/lib/NitroSDK/include-mw -I$ROOT/lib/NitroSDK/include-pokediamond"
M32="-m32 -malign-double"

fail=0

# The functions under test, linked with the test driver.
for f in os/os_vram_exclusive_clz os/os_spin_lock_id os/os_terminate_proc os/os_init \
         gx/gxasm gx/g3x_nop_clear_fifo; do
    src="$ROOT/lib/NitroSDK/src/$f.c"
    obj="$WORK/$(basename "$f").o"
    if ! "$CC" -c $M32 $CFLAGS $DEFS $INC "$src" -o "$obj"; then
        echo "FAIL $f: does not compile" >&2
        fail=1
    fi
done

# Compile-only: the busy waits, against the ARM7 lock words they poll.
for f in rtc/rtc_api mic; do
    src="$ROOT/lib/NitroSDK/src/$f.c"
    if ! "$CC" -c $M32 $CFLAGS $DEFS $INC "$src" -o "$WORK/$(basename "$f").o"; then
        echo "FAIL $f: does not compile" >&2
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "sdk small: FAILED to compile" >&2
    exit 1
fi

# Link-time stubs are in the driver; the rtc and mic objects are not linked.
# The port's memory arrays back HW_* under SDK_PORT_3DS (patch 1301); the SDK objects link against them.
if ! "$CC" -c $M32 -std=gnu99 "$HERE/../port_memory.c" -o "$WORK/port_memory.o"; then
    echo "sdk small: port_memory.c does not compile" >&2
    exit 1
fi
if ! "$CC" -c $M32 -std=gnu99 -Wall -Werror $DEFS "$HERE/test_small.c" -o "$WORK/test_small.o"; then
    echo "sdk small: test driver does not compile" >&2
    exit 1
fi
if ! "$CC" $M32 "$WORK/test_small.o" "$WORK/port_memory.o" \
        "$WORK/os_vram_exclusive_clz.o" "$WORK/os_spin_lock_id.o" "$WORK/os_terminate_proc.o" \
        "$WORK/os_init.o" "$WORK/gxasm.o" "$WORK/g3x_nop_clear_fifo.o" -o "$WORK/test_small"; then
    echo "sdk small: link failed" >&2
    exit 1
fi
if ! "$WORK/test_small"; then
    exit 1
fi
echo "sdk small: all ok"
