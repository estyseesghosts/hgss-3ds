# Dev container for the pokeheartgold 3DS port.
#
# Contains NO proprietary files: MWCC and the Nitro SDK enter only through the
# read-only /private bind mount (see devcontainer.json and onCreate.sh).
#
# Base matches the decomp's own image (ctrl-cheeb-del/pokeheartgold Dockerfile)
# and the pret/pmd-sky devcontainer pattern.
FROM --platform=linux/amd64 ubuntu:22.04

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
    xvfb \
 && ln -s python3 /usr/bin/python

# Port tooling: formatting, analysis, python libs, caching, debugging, gh
RUN apt-get install -y --no-install-recommends \
    clang-format clangd python3-pip python3-venv python3-pil python3-numpy python3-pyelftools \
    ccache gdb-multiarch gh

# Multilib g++-10 (host32 target needs -m32) and WineHQ with i386 multiarch.
RUN apt-get install -y --allow-downgrades g++-10-multilib gcc-10-multilib \
 && dpkg --add-architecture i386 \
 && mkdir -pm755 /etc/apt/keyrings \
 && wget -O /etc/apt/keyrings/winehq-archive.key https://dl.winehq.org/wine-builds/winehq.key \
 && add-apt-repository -y ppa:cybermax-dexter/sdl2-backport \
 && wget -NP /etc/apt/sources.list.d/ "https://dl.winehq.org/wine-builds/ubuntu/dists/$(lsb_release -cs)/winehq-$(lsb_release -cs).sources" \
 && apt-get update \
 && apt-get install -y --install-recommends --allow-downgrades winehq-stable \
 && apt-get clean && rm -rf /var/lib/apt/lists/*

# devkitPro: copied from the official image, then verified.
COPY --from=devkitpro/devkitarm:latest /opt/devkitpro /opt/devkitpro
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
