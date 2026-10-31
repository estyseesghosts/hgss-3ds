#!/bin/bash
# Matching build of the pinned decomp, with KEEP_SECTION blocks injected into the
# generated main.lcf. The decomp checkout is not edited; only its build artifact is.
#
# Usage: tools/matching-build.sh [DECOMP_DIR] [HEARTGOLD|SOULSILVER]
#
# Why: in the container (WineHQ 10.0 + MWCC 2.0/sp2p3 mwldarm, SDK 3.2 templates)
# the linker dead-strips five unreferenced assembly-data sections that the
# original ROM keeps, which moves .rodata by 0x800 and .bss by 0x20 and makes
# every SHA1 check fail. Keeping them reproduces the retail bytes exactly
# (HG US ROM SHA1 4fcded0e2713dc03929845de631d0932ea2b5a37, verified against a
# retail dump: 0 differing bytes). Sections, from the link map's
# "Unreferenced symbols" list for nitro_sdk_data.o and SND_util.o:
#   .rodata.unified_00 .rodata.unified_01 .rodata.snd_decibel
#   .bss.unified_00    .bss.unified_04
# The root cause of why the linker keeps them in the decomp authors' setup is
# NOT established (see docs/progress/phase-0.md).
set -euo pipefail
DECOMP="${1:-/work/pokeheartgold}"
GV="${2:-HEARTGOLD}"
case "$GV" in
    HEARTGOLD) NAME=heartgold.us ;;
    SOULSILVER) NAME=soulsilver.us ;;
    *) echo "unknown game version $GV" >&2; exit 1 ;;
esac
cd "$DECOMP"
make GAME_VERSION="$GV" "build/$NAME/main.lcf"
python3 - "build/$NAME/main.lcf" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
names = [".rodata.unified_00", ".rodata.unified_01", ".rodata.snd_decibel", ".bss.unified_00", ".bss.unified_04"]
if "# hgss-port keep" in s:
    print("keep blocks already present")
else:
    blocks = "# hgss-port keep\n" + "".join("KEEP_SECTION\n{\n\t%s\n}\n" % n for n in names)
    open(p, "w").write(blocks + s)
    print("injected", len(names), "KEEP_SECTION blocks")
PY
make -j"$(nproc)" GAME_VERSION="$GV"
