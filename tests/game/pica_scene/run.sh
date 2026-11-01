#!/bin/sh
# Host test for the 3DS GPU scene workaround (hgss/game/app/pica_scene.c).
# Plain C, no game data, no libctru. Run inside the dev container:
#   bash tests/game/pica_scene/run.sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CC=${CC:-gcc}
command -v "$CC" >/dev/null 2>&1 || CC=cc

OUT=$(mktemp -d "${TMPDIR:-/tmp}/hgss-pica-scene.XXXXXX")
trap 'rm -rf "$OUT"' EXIT INT TERM

"$CC" -std=c99 -O2 -Wall -Wextra -Werror \
    -I"$ROOT/3ds_port/nitro/include" \
    "$ROOT/tests/game/pica_scene/test_pica_scene.c" \
    "$ROOT/hgss/game/app/pica_scene.c" \
    -o "$OUT/test_pica_scene"

"$OUT/test_pica_scene"
