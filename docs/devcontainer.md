# Dev container

All building and testing happens in one amd64 Ubuntu 22.04 container defined in
`.devcontainer/`. Emulators (melonDS, Azahar), BizHawk, the 3DS and anything
with a GUI stay native on the host.

## macOS setup (Apple silicon)

1. Install **OrbStack** (`brew install orbstack`, open it once) or Docker
   Desktop. Both run amd64 images through Rosetta.
2. Install the Dev Containers CLI: `npm i -g @devcontainers/cli` (or use VS Code
   with the Dev Containers extension). Apple's `container` tool is not a
   supported Dev Containers backend.
3. Put your own proprietary files in `~/hgss-private` (below). Edit the mount
   line in `.devcontainer/devcontainer.json` if your folder is elsewhere.
4. `devcontainer up --workspace-folder .` then
   `devcontainer exec --workspace-folder . bash`.

### Rosetta vs QEMU

Wine's 32-bit (i386) code can fail under Rosetta. If MWCC crashes or `host32`
binaries won't run, switch the runtime to QEMU amd64 emulation (slower, works).
**Which one worked is recorded in `docs/progress/phase-0.md`.**

## Private folder (`~/hgss-private`, mounted read-only at `/private`)

```
~/hgss-private/
  mwccarm/                 license.dat, 1.2/, 2.0/{base,sp1,sp1p5,sp2,sp2p2,sp2p3,...}/
  NitroSDK/                extracted SDK: tools/bin/, include/nitro/specfiles/*.template
  roms/pokeheartgold.us.nds   pokesoulsilver.us.nds
  saves/*.sav
```

Never copied into a git-tracked location, never into an image layer.
`tools/install-ds-toolchain.sh` copies from `/private` into git-ignored paths of
the decomp checkout only.

## Volumes and paths

| Path | What | Where it lives |
| --- | --- | --- |
| `/private` | proprietary inputs | host bind mount, read-only |
| `/work` | decomp checkout (`/work/pokeheartgold`), Platinum clone (`/work/pokeplatinum-3ds`), `build/` trees, ccache | named volume `hgss-work` |
| `/workspaces/pokeheartgold-3ds` | this repo | host bind mount |
| `<repo>/build` | symlink to `/work/build` | volume (fast) |

Bind-mounted builds of thousands of files are slow on macOS, hence the volume.

## DS toolchain

`onCreate.sh` verifies `/private`, runs `xvfb-run wineboot` once (this must be in
the live container, not the Dockerfile), prints versions, and **never downloads
proprietary files**. Then:

```
tools/install-ds-toolchain.sh /work/pokeheartgold
cd /work/pokeheartgold && make -j$(nproc) && make GAME_VERSION=SOULSILVER -j$(nproc)
```

## Deploying to a 3DS

From the container: `3dslink -a <3DS IP> file.3dsx`. UDP broadcast discovery
doesn't cross the container's NAT, so always pass `-a`. Large transfers can
also go through ftpd from the Mac.

## devkitPro

Copied from `devkitpro/devkitarm:latest` (`COPY --from`). `verify-devkitpro.sh`
fails the image build if `3dsxtool`, `smdhtool`, `3dslink`, `tex3ds`,
`picasso`, libctru, citro3d or citro2d are missing; the fallback is devkitPro's
Debian pacman and `dkp-pacman -S 3ds-dev`. Which path was used is in
`docs/progress/phase-0.md`.
