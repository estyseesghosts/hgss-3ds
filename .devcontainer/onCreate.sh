#!/bin/bash
# Runs once when the dev container is created.
# Never downloads proprietary files: it only verifies that the user's own
# copies are present under the read-only /private mount.
set -u
WORKSPACE="${1:-$PWD}"
PRIVATE="${HGSS_PRIVATE:-/private}"

missing=()
[ -d "$PRIVATE" ] || missing+=("$PRIVATE is not mounted (set HGSS_PRIVATE on the host to your private folder)")
[ -f "$PRIVATE/mwccarm/2.0/sp2p2/mwccarm.exe" ] || missing+=("$PRIVATE/mwccarm/2.0/sp2p2/mwccarm.exe")
[ -f "$PRIVATE/mwccarm/license.dat" ] || missing+=("$PRIVATE/mwccarm/license.dat")
if [ ! -f "$PRIVATE/NitroSDK/tools/bin/makelcf.exe" ]; then
    missing+=("$PRIVATE/NitroSDK/tools/bin/makelcf.exe (an extracted Nitro SDK under $PRIVATE/NitroSDK)")
fi
for t in ARM7-TS.lcf.template ARM9-TS.lcf.template mwldarm.response.template; do
    [ -f "$PRIVATE/NitroSDK/include/nitro/specfiles/$t" ] || missing+=("$PRIVATE/NitroSDK/include/nitro/specfiles/$t")
done
if [ "${#missing[@]}" -ne 0 ]; then
    echo "ERROR: required private files are missing:" >&2
    printf '  - %s\n' "${missing[@]}" >&2
    echo "Nothing was downloaded. See docs/devcontainer.md for the expected layout." >&2
    exit 1
fi

# Build trees on the named volume (fast), reachable from the workspace.
mkdir -p /work/build
[ -e "$WORKSPACE/build" ] || ln -s /work/build "$WORKSPACE/build"

# Set up wine under a virtual X server so its first-run GUI never prompts
# during builds. Must run in the live container, not in the Dockerfile.
xvfb-run -a wineboot

echo "== toolchain versions =="
wine --version
arm-none-eabi-ld --version | head -1
gcc-10 --version | head -1
python3 --version
clang-format --version
[ -x "$DEVKITARM/bin/arm-none-eabi-gcc" ] && "$DEVKITARM/bin/arm-none-eabi-gcc" --version | head -1
for t in 3dsxtool smdhtool 3dslink tex3ds picasso; do command -v "$t" || echo "MISSING $t"; done
echo "onCreate OK"
