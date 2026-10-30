#!/bin/sh
# Fails the image build if the devkitPro tools the port needs are missing.
# If this fails, fall back to installing devkitPro's pacman for Debian and
# `dkp-pacman -S 3ds-dev`, and document that path in docs/devcontainer.md.
set -u
missing=0
for t in arm-none-eabi-gcc 3dsxtool smdhtool 3dslink tex3ds picasso; do
    command -v "$t" >/dev/null 2>&1 || { echo "missing tool: $t"; missing=1; }
done
for f in libctru/lib/libctru.a libctru/include/3ds.h \
         libctru/lib/libcitro3d.a libctru/include/citro3d.h \
         libctru/lib/libcitro2d.a libctru/include/citro2d.h; do
    [ -e "${DEVKITPRO}/$f" ] || { echo "missing file: ${DEVKITPRO}/$f"; missing=1; }
done
[ "$missing" = 0 ] && echo "devkitPro OK" || exit 1
