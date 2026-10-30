# Dev container for the pokeheartgold 3DS port.
#
# Contains NO proprietary files: MWCC and the Nitro SDK enter only through the
# read-only /private bind mount (see devcontainer.json and onCreate.sh).
#
# Base matches the decomp's own image (ctrl-cheeb-del/pokeheartgold Dockerfile)
# and the pret/pmd-sky devcontainer pattern.
FROM --platform=linux/amd64 ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
# We don't need Mono/Gecko; hide wine-internal "fixme" noise.
ENV WINEDLLOVERRIDES="mscoree=d;mshtml=d"
ENV WINEDEBUG="fixme-all"

# Basic utilities + the decomp's packages (its Dockerfile)
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
    ca-certificates software-properties-common gnupg curl sudo vim unzip bash-completion \
    wget lsb-release make git build-essential python3 pkg-config \
    p7zip-full libpng-dev libpugixml-dev linux-libc-dev binutils-arm-none-eabi \
    xvfb xauth \
 && ln -s python3 /usr/bin/python

# Port tooling: formatting, analysis, python libs, caching, debugging, gh
RUN apt-get install -y --no-install-recommends \
    clang-format clangd python3-pip python3-venv python3-pil python3-numpy python3-pyelftools \
    ccache gdb-multiarch gh

# Multilib g++-10 (host32 target needs -m32).
RUN apt-get install -y --allow-downgrades g++-10-multilib gcc-10-multilib

# Wine for MWCC and the SDK tools: WineHQ stable 10.0 (the decomp's image uses winehq-stable).
# Findings (docs/decisions.md): WineHQ 11.0 aborts in ntdll/unix/virtual.c (anon_mmap_fixed)
# under emulation; Ubuntu's distro Wine 9.0 runs MWCC but cannot load the Nitro SDK tools
# (fixed image base 0x400000, no relocations: "failed to create main module, c0000018");
# WineHQ 10.0 runs both.
RUN dpkg --add-architecture i386 \
 && mkdir -pm755 /etc/apt/keyrings \
 && wget -q -O /etc/apt/keyrings/winehq-archive.key https://dl.winehq.org/wine-builds/winehq.key \
 && wget -q -NP /etc/apt/sources.list.d/ https://dl.winehq.org/wine-builds/ubuntu/dists/noble/winehq-noble.sources \
 && apt-get update \
 && V=$(apt-cache madison winehq-stable | awk '{print $3}' | grep '^10\.0' | head -1) \
 && test -n "$V" \
 && apt-get install -y --no-install-recommends winehq-stable=$V wine-stable=$V wine-stable-amd64=$V wine-stable-i386=$V \
 && apt-get clean && rm -rf /var/lib/apt/lists/*

# devkitPro via its Debian/Ubuntu pacman. The devkitpro/devkitarm image's
# packages need glibc 2.36 and do not run on Ubuntu 22.04 (glibc 2.35), so
# COPY --from=devkitpro/devkitarm is not usable on 22.04, so this image is 24.04 (glibc 2.39).
RUN mkdir -p /usr/local/share/keyring \
 && wget -q -U "dkp apt" -O /usr/local/share/keyring/devkitpro-pub.gpg https://apt.devkitpro.org/devkitpro-pub.gpg \
 && echo "deb [signed-by=/usr/local/share/keyring/devkitpro-pub.gpg] https://apt.devkitpro.org stable main" > /etc/apt/sources.list.d/devkitpro.list \
 && apt-get update && apt-get install -y devkitpro-pacman \
 && ln -sf /proc/mounts /etc/mtab \
 && dkp-pacman -Syu --noconfirm 3ds-dev \
 && dkp-pacman -Scc --noconfirm \
 && apt-get clean && rm -rf /var/lib/apt/lists/*
ENV DEVKITPRO=/opt/devkitpro
ENV DEVKITARM=/opt/devkitpro/devkitARM
ENV PATH="${DEVKITPRO}/tools/bin:${DEVKITARM}/bin:${PATH}"
COPY verify-devkitpro.sh /opt/devcontainer/verify-devkitpro.sh
RUN /opt/devcontainer/verify-devkitpro.sh

# Non-root user with passwordless sudo (as pmd-sky does)
RUN useradd -m -s /bin/bash -g sudo hgss \
 && echo "hgss ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/hgss \
 && mkdir -p /work && chown hgss:sudo /work
USER hgss

ENV CC=gcc-10
ENV CXX=g++-10

COPY onCreate.sh /opt/devcontainer/onCreate.sh
