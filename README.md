# pokeheartgold-3ds

A native 3DS port of Pokémon HeartGold and SoulSilver, built from the [ctrl-cheeb-del/pokeheartgold](https://github.com/ctrl-cheeb-del/pokeheartgold) decompilation. The hardware layer comes from [GabeSud0/pokeplatinum-3ds](https://github.com/GabeSud0/pokeplatinum-3ds): a virtual ARM7, DS memory and I/O registers as RAM, software 2D, G3 to PICA200 3D, a sound chip model, and a raw `.sav` save service. The project is licensed under GPL-3.0.

**No game runs yet.** This repo has the build scaffolding, the SDK patches, the test suites and the tools. It does not boot HeartGold or SoulSilver, and it does not produce a 3DS build.

The wiki is the place to start: [docs/wiki/Home.md](docs/wiki/Home.md).

## Current status

- **Phase 0 (baseline): complete, with open items.** Both US versions of the decomp build byte-identically inside the dev container, checked against their SHA1s and (for HeartGold) against a retail dump. In the container this needs `tools/matching-build.sh`, not plain `make`: the linker there strips five sections the original keeps, and the script restores them in the generated linker file. Still open: the golden reference captures (never run), the fixture saves, and the CI matching job, which is disabled. Details: [docs/progress/phase-0.md](docs/progress/phase-0.md).
- **Phase 1 (SDK patch stack): partly done.** Numbered patches (the list is in [docs/wiki/Patch-Stack.md](docs/wiki/Patch-Stack.md)), recorded as applying cleanly from scratch. Of the 284 NitroSDK and NitroSystem source files, 270 pass a GCC syntax check under `SDK_PORT_3DS`; the other 14 are files the port replaces. Each patch has a host test, or the layout probe. The save-struct layout matches MWCC for 804 values. The vendored port layer's own unit tests are not wired to HGSS yet, and `make port-tests` fails on purpose. Details: [docs/progress/phase-1.md](docs/progress/phase-1.md).
- **Phases 2 to 11: not started.** No game code is compiled or linked. There is no ROM builder (`builder/` does not exist), no 3DS shell for HGSS, and no golden frames. None of the sound, IR, wireless or GPU 2D phases has started.

What you can run today: `make test` (19 Python tests and nine SDK host suites), `make layout` (the save-layout probe, 0 differences), and the matching builds for HeartGold and SoulSilver, if you have the toolchain.

## Content rules

This repo never holds game content. It contains no ROMs, no extracted assets, no saves, no golden frames, no MWCC or Nitro SDK files, and no built game binaries. The container gets your MWCC and SDK files only from `~/hgss-private`, which is mounted read-only. You supply everything proprietary yourself. See [NOTICE.md](NOTICE.md) and [CONTRIBUTING.md](CONTRIBUTING.md).

## Requirements

- An Apple-silicon Mac with OrbStack (tested) or Docker Desktop (documented, not tested here), and Node.js for the Dev Containers CLI. This is the setup the repo documents in full.
- Linux: `.github/workflows/port.yml` is written to build the image and run `make test` on Ubuntu, but it has not run on GitHub yet. The DS-side steps are documented for the macOS container only.
- Your own copies of the MWCC 2.0 archive (`mwccarm.zip`, with `license.dat`) and the Nitro SDK 3.2-060901 archive. The container refuses to set up without them.
- Optional: your US HeartGold or SoulSilver ROM and a save. The steps below do not use them.

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
  Makefile            make test, sdk-survey, layout, port-tests (fails on purpose), clean
  3ds_port/           hardware layer from pokeplatinum-3ds: nitro, nitrosystem, pak, game
                      (game/ is still Platinum's shell; HGSS's own shell is not written)
  patches/            numbered patches for the decomp, in three stacks: NitroSDK, NitroSystem, pokeheartgold
  tools/              bootstrap, toolchain install, matching build, surveys, savetool,
                      layout probe, golden capture scripts
  tests/              Python tests, SDK host suites (tests/sdk/*), layout probe runner, input scripts
  docs/               progress records, decision log, pins, and the wiki in docs/wiki/
  .devcontainer/      dev container image and its setup scripts
  .github/workflows/  port.yml (public CI; not yet run on GitHub) and matching.yml (disabled)
  build/              git-ignored output; a Docker volume inside the container
```

## Where to read next

- [docs/wiki/Home.md](docs/wiki/Home.md): the wiki index.
- [docs/wiki/Getting-Started.md](docs/wiki/Getting-Started.md): setup, step by step, with troubleshooting.
- [docs/wiki/Daily-Workflow.md](docs/wiki/Daily-Workflow.md): the loop for working on the port.
- [docs/wiki/Tools-Reference.md](docs/wiki/Tools-Reference.md): every script and test runner.
- [gameplan.md](gameplan.md): the plan.
- [docs/decisions.md](docs/decisions.md): what was decided, and why.

## License

GPL-3.0. See [LICENSE](LICENSE). The decomp and the game are not covered by it, and they are not in this repo. See [NOTICE.md](NOTICE.md).
