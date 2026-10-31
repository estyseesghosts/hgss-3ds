# Dev container

All building and testing happens in one amd64 Ubuntu 24.04 container defined in
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

Measured on an M1 Pro with OrbStack: **Rosetta works** for WineHQ 10.0 (MWCC, SDK tools),
the full matching builds and 32-bit `host32` binaries; it prints harmless
`rosetta error: invalid gdt selector` lines. WineHQ 11.0 aborts under both Rosetta and
QEMU, and Ubuntu's distro Wine 9.0 cannot load the Nitro SDK tools, hence the pinned
10.0 (`docs/decisions.md`). QEMU (`orbctl config set rosetta false`) was only tried with
the failing Wines. Details: `docs/progress/phase-0.md`.

## Private folder (`~/hgss-private`, mounted read-only at `/private`)

```
~/hgss-private/
  mwccarm/                 license.dat, 1.2/, 2.0/{base,sp1,sp1p5,sp2,sp2p2,sp2p3,...}/
  NitroSDK/                extracted NitroSDK-3_2-060901 (SHA256 of the archive 0ee935e3...): tools/bin/, include/nitro/specfiles/*.template
  heartgold-retail.nds     optional: your retail ROM, used to diagnose matching failures
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
| `<repo>/build` | patched upstream checkout and outputs | named volume `hgss-build`, mounted over the workspace's `build/` (fast; the host sees an empty directory) |

Bind-mounted builds of thousands of files are slow on macOS, hence the volume.

## DS toolchain

`onCreate.sh` verifies `/private`, runs `xvfb-run wineboot` once (this must be in
the live container, not the Dockerfile), prints versions, and **never downloads
proprietary files**. Then:

```
tools/install-ds-toolchain.sh /work/pokeheartgold
tools/matching-build.sh /work/pokeheartgold HEARTGOLD    # plain `make` does not match here: it keeps five
tools/matching-build.sh /work/pokeheartgold SOULSILVER   # sections the linker strips (docs/progress/phase-0.md)
```

## Deploying to a 3DS

From the container: `3dslink -a <3DS IP> file.3dsx`. UDP broadcast discovery
doesn't cross the container's NAT, so always pass `-a`. Large transfers can
also go through ftpd from the Mac.

## devkitPro

Installed with devkitPro's apt repo and `dkp-pacman -S 3ds-dev` (the `COPY --from=devkitpro/devkitarm`
route does not work: its binaries need glibc 2.36). `apt.devkitpro.org` rejects the default
User-Agent with a Cloudflare 403, so the Dockerfile fetches the key with `wget -U "dkp apt"`.
`verify-devkitpro.sh` runs the tools and fails the image build if any is missing or does not run.
