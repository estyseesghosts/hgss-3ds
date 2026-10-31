#!/usr/bin/env bash
# Host tests for the SDK_PORT_3DS hooks in lib/NitroSDK/src:
#   test_irq_clear.c  os/os_interrupt.c, os/os_tick.c   IRQ requests cleared by bits (Platinum 0017)
#   test_dma_hooks.c  mi/mi_dma*.c                      software DMA start hooks (Platinum 0009)
#   test_gx_sync.c    gx/g3x.c                          matrix result sync before reads (Platinum 0026)
# Usage: tests/sdk/port_hooks/run.sh [ROOT]   (patched decomp tree, default build/upstream;
# run inside the dev container). Uses the survey tool's defines/flags/includes. Exits non-zero on any failure.
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
OUT=$(mktemp -d "${TMPDIR:-/tmp}/port_hooks_test.XXXXXX")
trap 'rm -rf "$OUT"' EXIT

compile() { # compile SRC OBJ
    "$GCC" -m32 -malign-double "${FLAGS[@]}" -c "$1" -o "$2"
}
SRC="$ROOT/lib/NitroSDK/src"

compile "$SRC/os/os_system.c" "$OUT/os_system.o"
compile "$SRC/os/os_interrupt.c" "$OUT/os_interrupt.o"
compile "$SRC/os/os_tick.c" "$OUT/os_tick.o"
compile "$HERE/../port_memory.c" "$OUT/port_memory.o"
compile "$HERE/test_irq_clear.c" "$OUT/test_irq_clear.o"
"$GCC" -m32 "$OUT/test_irq_clear.o" "$OUT/port_memory.o" "$OUT/os_system.o" "$OUT/os_interrupt.o" \
    "$OUT/os_tick.o" -o "$OUT/test_irq_clear"
# A missing hook leaves the SDK's busy-wait on the channel spinning, so every run is time-limited.
timeout 60 "$OUT/test_irq_clear"

for f in mi_dma mi_dma_hblank mi_dma_card mi_dma_gxcommand; do
    compile "$SRC/mi/$f.c" "$OUT/$f.o"
done
compile "$HERE/test_dma_hooks.c" "$OUT/test_dma_hooks.o"
"$GCC" -m32 "$OUT/test_dma_hooks.o" "$OUT/port_memory.o" "$OUT/os_system.o" "$OUT/mi_dma.o" \
    "$OUT/mi_dma_hblank.o" "$OUT/mi_dma_card.o" "$OUT/mi_dma_gxcommand.o" -o "$OUT/test_dma_hooks"
timeout 60 "$OUT/test_dma_hooks"

compile "$SRC/gx/g3x.c" "$OUT/g3x.o"
compile "$HERE/test_gx_sync.c" "$OUT/test_gx_sync.o"
"$GCC" -m32 "$OUT/test_gx_sync.o" "$OUT/port_memory.o" "$OUT/g3x.o" -o "$OUT/test_gx_sync"
timeout 60 "$OUT/test_gx_sync"
