#!/bin/sh
# Host test for the 3DS scripted-input parser (hgss/game/app/3ds_input.c).
# Plain C, no game data, no libctru. Run inside the dev container:
#   bash tests/game/script_input/run.sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CC=${CC:-gcc}
command -v "$CC" >/dev/null 2>&1 || CC=cc

OUT=$(mktemp -d "${TMPDIR:-/tmp}/hgss-3ds-input.XXXXXX")
trap 'rm -rf "$OUT"' EXIT INT TERM

"$CC" -std=c99 -O2 -Wall -Wextra -Werror \
    -I"$ROOT/hgss/game/app" \
    "$ROOT/tests/game/script_input/test_3ds_input.c" \
    "$ROOT/hgss/game/app/3ds_input.c" \
    -o "$OUT/test_3ds_input"

"$OUT/test_3ds_input"
