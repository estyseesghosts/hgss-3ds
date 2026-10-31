#!/bin/sh
# Host known-answer test for the plain-C RC4 functions added under SDK_PORT_3DS
# to lib/src/nitrocrypto_rc4.c and lib/src/nitrocrypto_rc4_fast.c.
#
# Compiles only those two translation units and the key-schedule file they pair
# with (nitrocrypto_rc4_init.c); no game data and no other game source is
# needed. Run inside the dev container after tools/bootstrap.py:
#
#   bash tests/game/rc4/run.sh
#
# Set HGSS_UPSTREAM to point at a different patched checkout.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
UPSTREAM=${HGSS_UPSTREAM:-"$ROOT/build/upstream"}
CC=${CC:-gcc-10}
command -v "$CC" >/dev/null 2>&1 || CC=cc

for f in lib/src/nitrocrypto_rc4.c lib/src/nitrocrypto_rc4_fast.c \
         lib/src/nitrocrypto_rc4_init.c include/library/nitrocrypto_rc4.h; do
    if [ ! -f "$UPSTREAM/$f" ]; then
        echo "missing $UPSTREAM/$f; run: python3 tools/bootstrap.py" >&2
        exit 1
    fi
done

OUT=$(mktemp -d "${TMPDIR:-/tmp}/hgss-rc4.XXXXXX")
trap 'rm -rf "$OUT"' EXIT INT TERM

"$CC" -std=c99 -O2 -Wall -Wextra -Werror -Wno-unknown-pragmas -m32 -malign-double \
    -I"$UPSTREAM/lib/NitroSDK/include-pokediamond" \
    -I"$UPSTREAM/include/library" \
    -DSDK_PORT_3DS \
    "$ROOT/tests/game/rc4/test_rc4.c" \
    "$UPSTREAM/lib/src/nitrocrypto_rc4.c" \
    "$UPSTREAM/lib/src/nitrocrypto_rc4_fast.c" \
    "$UPSTREAM/lib/src/nitrocrypto_rc4_init.c" \
    -o "$OUT/test_rc4"

"$OUT/test_rc4"
