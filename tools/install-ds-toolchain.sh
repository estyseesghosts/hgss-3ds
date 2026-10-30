#!/bin/bash
# Copy the user's own MWCC and Nitro SDK files from the read-only private
# folder into git-ignored paths of the decomp checkout. Never downloads
# anything and never writes into a tracked location.
#
# Usage: tools/install-ds-toolchain.sh [DECOMP_DIR] [PRIVATE_DIR]
#   DECOMP_DIR  default /work/pokeheartgold
#   PRIVATE_DIR default $HGSS_PRIVATE or /private
#
# Layout installed (per the decomp's INSTALL.md):
#   tools/mwccarm/            <- $PRIVATE/mwccarm/   (whole tree)
#   tools/bin/                <- $PRIVATE/NitroSDK/tools/bin
#   sub/ARM7-TS.lcf.template  <- $PRIVATE/NitroSDK/include/nitro/specfiles
#   ARM9-TS.lcf.template, mwldarm.response.template (repo root) <- same dir
#
# Why the whole mwccarm tree and not only 2.0/sp2p2: the decomp's common.mk
# builds lib/NitroSDK and lib/MSL_C with 2.0/sp2p3, and other files pick
# 1.2/sp2p3, 2.0/base, sp1, sp1p5 and sp2 (grep MWCCVER in the decomp).
# Why license.dat sits in tools/mwccarm/: common.mk exports
# LM_LICENSE_FILE := $(TOOLSDIR)/mwccarm/license.dat, which is how the
# license is registered; no GUI step is needed.
set -euo pipefail

DECOMP="${1:-/work/pokeheartgold}"
PRIVATE="${2:-${HGSS_PRIVATE:-/private}}"
SPEC="$PRIVATE/NitroSDK/include/nitro/specfiles"

die() { echo "ERROR: $*" >&2; exit 1; }

[ -d "$DECOMP/.git" ] || die "$DECOMP is not a git checkout"
for f in "$PRIVATE/mwccarm/2.0/sp2p2/mwccarm.exe" "$PRIVATE/mwccarm/2.0/sp2p3/mwccarm.exe" \
         "$PRIVATE/mwccarm/license.dat" "$PRIVATE/NitroSDK/tools/bin/makelcf.exe" \
         "$SPEC/ARM7-TS.lcf.template" "$SPEC/ARM9-TS.lcf.template" "$SPEC/mwldarm.response.template"; do
    [ -f "$f" ] || die "missing $f"
done

# Refuse to write anywhere git would track.
copy() {  # copy SRC DEST_REL (recursive for dirs)
    local src="$1" rel="$2"
    # directories are checked via a child path (patterns like tools/mwccarm/** match contents)
    local probe="$rel"; [ -d "$src" ] && probe="$rel/x"
    git -C "$DECOMP" check-ignore -q "$probe" || die "$rel is not git-ignored in $DECOMP; refusing to copy"
    mkdir -p "$(dirname "$DECOMP/$rel")"
    rm -rf "$DECOMP/$rel"
    cp -R "$src" "$DECOMP/$rel"
    echo "installed $rel"
}

copy "$PRIVATE/mwccarm" tools/mwccarm
copy "$PRIVATE/NitroSDK/tools/bin" tools/bin
copy "$SPEC/ARM7-TS.lcf.template" sub/ARM7-TS.lcf.template
copy "$SPEC/ARM9-TS.lcf.template" ARM9-TS.lcf.template
copy "$SPEC/mwldarm.response.template" mwldarm.response.template

# Smoke-test the compiler under wine (also warms the wine prefix).
if command -v wine >/dev/null; then
    out=$(cd "$DECOMP" && LM_LICENSE_FILE="$DECOMP/tools/mwccarm/license.dat" xvfb-run -a wine tools/mwccarm/2.0/sp2p2/mwccarm.exe 2>&1 | head -3 || true)
    echo "mwccarm under wine: ${out:-<no output>}"
fi
