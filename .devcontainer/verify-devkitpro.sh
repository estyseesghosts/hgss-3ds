#!/bin/sh
# Fails the image build if the devkitPro tools the port needs are missing.
set -u
missing=0
# Run each tool, not just find it: a binary built for a newer glibc is "present" but unusable.
arm-none-eabi-gcc --version || { echo "arm-none-eabi-gcc does not run (PATH=$PATH)"; ls ${DEVKITARM}/bin | head -3; missing=1; }
for t in 3dsxtool smdhtool 3dslink tex3ds picasso; do
    command -v "$t" >/dev/null 2>&1 || { echo "missing tool: $t"; missing=1; continue; }
    "$t" --version >/dev/null 2>&1 || "$t" --help >/dev/null 2>&1 || "$t" >/dev/null 2>&1 || true
done
for f in libctru/lib/libctru.a libctru/include/3ds.h \
         libctru/lib/libcitro3d.a libctru/include/citro3d.h \
         libctru/lib/libcitro2d.a libctru/include/citro2d.h; do
    [ -e "${DEVKITPRO}/$f" ] || { echo "missing file: ${DEVKITPRO}/$f"; missing=1; }
done
[ "$missing" = 0 ] && echo "devkitPro OK" || exit 1
