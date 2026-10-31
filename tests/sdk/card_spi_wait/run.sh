#!/usr/bin/env bash
# Host test for the SDK_PORT_3DS CARD_SpiWait stub in lib/NitroSDK/src/card/card_spi_wait*.c.
# Usage: tests/sdk/card_spi_wait/run.sh [ROOT]   (patched decomp tree, default build/upstream;
# run inside the dev container). Uses the survey tool's defines/flags/includes.
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
OUT=$(mktemp -d "${TMPDIR:-/tmp}/card_spi_wait_test.XXXXXX")
trap 'rm -rf "$OUT"' EXIT
for f in card_spi_wait card_spi_wait_init card_spi_wait_cycles; do
    "$GCC" -m32 -malign-double "${FLAGS[@]}" -c "$ROOT/lib/NitroSDK/src/card/$f.c" -o "$OUT/$f.o"
done
"$GCC" -m32 -malign-double "${FLAGS[@]}" -c "$HERE/../port_memory.c" -o "$OUT/port_memory.o"
"$GCC" -m32 -malign-double "${FLAGS[@]}" -c "$HERE/test_card_spi_wait.c" -o "$OUT/test.o"
"$GCC" -m32 "$OUT/test.o" "$OUT/port_memory.o" "$OUT/card_spi_wait.o" "$OUT/card_spi_wait_init.o" \
    "$OUT/card_spi_wait_cycles.o" -o "$OUT/test"
"$OUT/test"
