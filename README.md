# pokeheartgold-3ds

A native 3DS port of Pokémon HeartGold and SoulSilver, built from the [ctrl-cheeb-del/pokeheartgold](https://github.com/ctrl-cheeb-del/pokeheartgold) decompilation. The hardware layer comes from [GabeSud0/pokeplatinum-3ds](https://github.com/GabeSud0/pokeplatinum-3ds): a virtual ARM7, DS memory and I/O registers as RAM, software 2D, G3 to PICA200 3D, a sound chip model, and a raw `.sav` save service. The project is licensed under GPL-3.0.

**It runs on a New 3DS.** The build boots HeartGold, plays the intro, reaches
Oak's speech and the player's bedroom, draws both screens with the PICA 3D
layer, and plays audio. The latest artifact (frame cap + audio + camera-point
fixes) is staged for a hardware re-run; its visual/audio/timing results are
still pending. This is not a finished port: the save round-trip, Old 3DS and
`.cia` decisions are open. The living status is
[docs/progress/STATE.md](docs/progress/STATE.md).

The wiki is the place to start: [docs/wiki/Home.md](docs/wiki/Home.md).

## Current status

- **Phase 0 (baseline): complete, with open items.** Both US versions of the decomp build byte-identically inside the dev container, checked against their SHA1s and (for HeartGold) against a retail dump. In the container this needs `tools/matching-build.sh`, not plain `make`: the linker there strips five sections the original keeps, and the script restores them in the generated linker file. Still open: the golden reference captures (never run), the fixture saves, and the CI matching job, which is disabled. Details: [docs/progress/phase-0.md](docs/progress/phase-0.md).
- **Phase 1 (SDK patch stack): done.** **78 patches** apply cleanly from scratch (NitroSDK 15, NitroSystem 3, pokeheartgold 60; the catalog is [docs/wiki/Patch-Stack.md](docs/wiki/Patch-Stack.md)). `make test` is green (**338 Python tests + 20 SDK suites**), `make port-tests` exits 0 (nitrosystem **17,122 checks, 0 failed**), and the DS gate at 78 reproduces both retail ROMs byte for byte (HG `4fcded0e…b5a37`, SS `f8dc38ea…e582`). The save-struct layout matches MWCC for 804 values (0 differences). Details: [docs/progress/STATE.md](docs/progress/STATE.md).
- **Phase 2 (game under GCC): done for the exit criterion.** 1092/1092 game files compile on host32 and 3DS, 150/150 ARM9 `.s` assemble, both port archives build, and both programs link with **0 unresolved**.
- **Phase 3 (data pipeline): done.** `builder/{verify_rom,extract_rom,pack}.py` make the HG/SS pack from your own cartridge; the real HG pack opens in the PC program and the boot errors are mapped.
- **Phase 4 (boot to Oak): in progress.** The PC build reaches the logo, movie, title, main menu, Oak's speech, naming and the first bedroom; boot smoke tests and scripted inputs exist. Golden captures and the save round-trip are open.
- **Phase 5 (3DS): in progress, runs on hardware.** Platinum's classic PICA backend is compiled into the HGSS shell (geometry thread + GPU worker), 2D/3D composite on the two screens, NDSP audio, scripted input and APT close. The ninth run showed the 3D at ~87 Hz; the latest tree adds a **60 Hz frame cap**, the **noise-channel register fix** and the **camera-point 3D fix**. Its artifact is staged; a hardware re-run is pending.

What you can run today: `make test`, `make port-tests`, `make content-check`, `make layout`, both surveys, `make game-pc`, `make game-3ds` (packages `pokeheartgold.3dsx`), the data builder, and the DS matching builds for HeartGold and SoulSilver with the toolchain.

## Content rules

This repo never holds game content. It contains no ROMs, no extracted assets, no saves, no golden frames, no MWCC or Nitro SDK files, and no built game binaries. The container gets your MWCC and SDK files only from `~/hgss-private`, which is mounted read-only. You supply everything proprietary yourself. See [NOTICE.md](NOTICE.md) and [CONTRIBUTING.md](CONTRIBUTING.md).

## Requirements

- An Apple-silicon Mac with OrbStack (tested) or Docker Desktop (documented, not tested here), and Node.js for the Dev Containers CLI. This is the setup the repo documents in full.
- Linux: `.github/workflows/port.yml` builds the image and runs `make test` on GitHub; the first public push passed (content check, image build, bootstrap, tests, 3m3s). The DS-side steps are documented for the macOS container only.
- Your own copies of the MWCC 2.0 archive (`mwccarm.zip`, with `license.dat`) and the Nitro SDK 3.2-060901 archive. The container refuses to set up without them.
- Your own US HeartGold or SoulSilver ROM to build the data pack (`builder/`) and run the game. The setup and test steps above do not use it; the game does. A save is optional.

## Quick start

Full steps, with what you should see at each one, are in [docs/wiki/Getting-Started.md](docs/wiki/Getting-Started.md). The short version, from your checkout on the Mac:

```
brew install orbstack                       # open OrbStack once
npm i -g @devcontainers/cli
# put your MWCC and Nitro SDK files in ~/hgss-private (see Getting-Started, step 3)
devcontainer up --workspace-folder .
devcontainer exec --workspace-folder . bash -c 'git clone https://github.com/ctrl-cheeb-del/pokeheartgold /work/pokeheartgold && git -C /work/pokeheartgold checkout "$(sed -n "s/^commit=//p" /workspaces/pokeheartgold-3ds/upstream.lock)"'
devcontainer exec --workspace-folder . bash -c 'cd /workspaces/pokeheartgold-3ds && tools/install-ds-toolchain.sh /work/pokeheartgold'
devcontainer exec --workspace-folder . bash -c 'cd /workspaces/pokeheartgold-3ds && python3 tools/bootstrap.py'
devcontainer exec --workspace-folder . bash -c 'cd /workspaces/pokeheartgold-3ds && make test'
```

The matching build is the DS-side check. It is slow the first time (about 18 minutes in Phase 0). It is in step 7 of Getting-Started, and it is not needed for `make test`.

## Repository layout

```
pokeheartgold-3ds/
  README.md           this file
  gameplan.md         the plan: phases, layers, save specification, risk register
  upstream.lock       pinned decomp commit; build/upstream is fetched at it
  platinum.lock       pokeplatinum-3ds commit that 3ds_port/ comes from
  Makefile            make test, sdk-survey, layout, port-tests, game-pc, game-3ds, clean
  3ds_port/           hardware layer from pokeplatinum-3ds: nitro, nitrosystem, pak, game
                      (HGSS's own shell is hgss/game/app; one deliberate upstream-style
                      camera-point fix diverges from platinum.lock, see docs/decisions.md)
  patches/            numbered patches for the decomp, in three stacks: NitroSDK, NitroSystem, pokeheartgold
  builder/            verify/extract/pack the user's own ROM into the data pack
  tools/              bootstrap, toolchain install, matching build, surveys, savetool,
                      layout probe, golden capture scripts, content check
  tests/              Python tests, SDK host suites (tests/sdk/*), layout probe runner, input scripts
  docs/               progress records, decision log, pins, the wiki, and docs/upstream/ patches
                      for third-party projects (the Platinum camera-point fix)
  .devcontainer/      dev container image and its setup scripts
  .github/workflows/  port.yml (public CI; green on the first push) and matching.yml (disabled)
  build/              git-ignored output; a Docker volume inside the container
```

## Where to read next

- [docs/wiki/Home.md](docs/wiki/Home.md): the wiki index.
- [docs/MONOREPO.md](docs/MONOREPO.md): what is in the monorepo, what is deliberately outside, and how to stage/push it to GitHub, including the Platinum port patch.
- [docs/wiki/Build-Reference.md](docs/wiki/Build-Reference.md): the toolchain, every build target and its exact command.
- [docs/wiki/Getting-Started.md](docs/wiki/Getting-Started.md): setup, step by step, with troubleshooting.
- [docs/wiki/Daily-Workflow.md](docs/wiki/Daily-Workflow.md): the loop for working on the port.
- [docs/wiki/Tools-Reference.md](docs/wiki/Tools-Reference.md): every script and test runner.
- [docs/progress/STATE.md](docs/progress/STATE.md): the living status page: what is green today, what is next.
- [gameplan.md](gameplan.md): the plan.
- [docs/decisions.md](docs/decisions.md): what was decided, and why.

## License

GPL-3.0. See [LICENSE](LICENSE). The decomp and the game are not covered by it, and they are not in this repo. See [NOTICE.md](NOTICE.md).
