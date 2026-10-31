#!/usr/bin/env bash
# Fails if any save-block struct size or member offset differs between MWCC (the committed
# tools/layout_probe/mwcc.txt) and GCC (host 32-bit, host 32-bit with 8-byte doubles, devkitARM).
# Run inside the dev container after `python3 tools/bootstrap.py`. Needs pyelftools (a venv is fine:
# PY=/work/venv/bin/python) and the decomp's generated .naix headers (FILES=/work/pokeheartgold/files).
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
cd "$REPO"
exec "${PY:-python3}" tools/layout_probe/run_probe.py --root build/upstream --decomp "${DECOMP:-/work/pokeheartgold}" \
    ${FILES:+--files "$FILES"} "$@"
