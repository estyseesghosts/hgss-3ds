# pokeheartgold-3ds

**Pokémon HeartGold & SoulSilver, running natively on the 3DS.**

This is a port of the [pokeheartgold decompilation](https://github.com/ctrl-cheeb-del/pokeheartgold),
with the DS hardware layer from [pokeplatinum-3ds](https://github.com/GabeSud0/pokeplatinum-3ds):
a virtual ARM7, DS memory and I/O as ordinary RAM, software 2D, a G3 → PICA200 3D
translator, a sound-chip model, and a raw `.sav` save service. Both games build, boot
and play on real hardware and in Azahar.

It is GPL-3.0, and it contains no game content: you bring your own ROM.

> 🌱 **The short version:** the game runs, all of it — field, battles, menus, audio,
> both screens at 400×240 / 320×240 with native-resolution PICA 3D. The work left is
> *speed*: an Old 3DS presents about six frames a second in the field today, and the
> drawer thread is what bounds it. Everything below is honest about that.

## What works today

| | |
|---|---|
| **Boot and play** | Both games reach the title, main menu, Oak's speech, the player's bedroom and the field. Battles render both Pokémon (720 tris/scene, 0 clipped). |
| **Display** | Platinum's remaster compositor: upper 400×240 (320×192 canvas at 1.25×), lower 320×240, 3D drawn at native resolution, **no classic 1:1 fallback** — a failed display shows an error screen instead of quietly degrading. |
| **Audio** | The DS mixer through NDSP (32768 Hz mixed, ~32728.5 Hz out, no resampling). |
| **Input, saves, closing** | Scripted input replay, raw 512 KiB saves with CRC repair, touch mapped back to 256×192, clean START+SELECT and HOME exits. |
| **Builds** | One tree builds HeartGold and SoulSilver (`pokeheartgold.3dsx`, `pokesoulsilver.3dsx`), on Linux, macOS and — natively, no WSL — [Windows](docs/WINDOWS.md). |

**Performance, measured on a real Old 3DS** (Goldenrod, field at rest, 2026-11-10):

| metric | value |
|---|---|
| presented frames | ~6/s (drawer 92 ms/frame, ~50 ms of it the top-screen pass) |
| composes | 8.6–9.8/s, lag 4 V-blanks |
| geometry | 35 ms/scene at ~100% busy — it builds ~27 scenes/s for the ~6 that are shown |
| GPU | 12.1 ms/frame, of which 6.1 ms is clears — the PICA is ~12% busy |
| game clock | ≤55–57 Hz (slightly under 60) |

So the GPU is idle and the CPU drawer is the wall. The current campaign is about
feeding the GPU cheaply; the evidence trail lives in
[docs/bench/](docs/bench/README.md) and [docs/progress/STATE.md](docs/progress/STATE.md).

## Just want to play?

You do not need the container, MWCC or the Nitro SDK. Three Python scripts on
Linux, macOS or Windows:

```sh
python3 scripts/setup_toolchain.py                        # devkitARM, libctru, 3dsxtool
python3 scripts/prepare_game.py ~/Downloads/game.nds      # your own HG or SS dump
python3 scripts/build_3dsx.py --pak build/heartgold3ds.pak
# copy dist/sd-card/3ds onto the SD card, then start it from the Homebrew Launcher
```

Full steps, with what you should see at each one: [the player's guide](docs/USER-GUIDE.md).
On Windows: [docs/WINDOWS.md](docs/WINDOWS.md) (native, tested).

## Contributing

The short version: no game content in the repo, the decompilation is edited only
through numbered patches, and the DS matching build must stay byte-identical.

```sh
git clone --recurse-submodules <this repo>
python3 tools/bootstrap.py            # fetch the pinned decomp, apply the patches
make test                             # Python suite + 20 SDK host suites
```

The developer setup (dev container, your private MWCC and Nitro SDK, the DS
toolchain, the matching build) is in
[docs/wiki/Getting-Started.md](docs/wiki/Getting-Started.md); the build lookup
table is [docs/wiki/Build-Reference.md](docs/wiki/Build-Reference.md). Read
[docs/wiki/3DS-Pitfalls.md](docs/wiki/3DS-Pitfalls.md) before touching the shell.

## Status, by phase

| phase | state |
|---|---|
| 0 · baseline | Both US decompilations build byte-identically; golden captures and the CI matching job are open. |
| 1 · SDK patch stack | 87 patches (NitroSDK, NitroSystem, pokeheartgold). `make port-tests` green; save layout matches MWCC for 804 values. |
| 2 · game under GCC | 1092/1092 files compile on host32 and 3DS; both programs link with 0 unresolved. |
| 3 · data pipeline | `builder/` makes the HG/SS pack from your own cartridge. |
| 4 · boot to Oak | Reached on the PC reference; save round-trip open. |
| 5 · 3DS | Phase A remaster **runs on hardware**; the open work is performance, not bring-up. |

The living status page — what is green today, what was just measured, what is next —
is [docs/progress/STATE.md](docs/progress/STATE.md).

## Repository layout

```
pokeheartgold-3ds/
  README.md            this file
  gameplan.md          the plan: phases, layers, save specification, risk register
  PICA200-BUILDING.md      implementer's guide to the GPU and the work packets
  PICA200-INSTRUMENTATION.md  the evidence/measurement contract
  HGSS_3DS_ENGINEERING_GUIDE.md  the standing engineering rules
  upstream.lock        pinned decomp commit        platinum.lock  pinned port commit
  3ds_port/            the DS hardware layer (nitro, nitrosystem, pak, game)
  hgss/game/app/       the 3DS shell: display, drawers, geometry, audio
  patches/             numbered patches: NitroSDK, NitroSystem, pokeheartgold
  scripts/             player-facing: setup_toolchain, prepare_game, build_3dsx
  tools/               bootstrap, surveys, savetool, fixtures, and the bench tools
                       (manifest, report, A/B driver, SD staging)
  tests/               Python suite, SDK host suites, layout probe, input scripts
  docs/                wiki, progress records, decision log, bench packets and
                       claim sheets, the player's guide and the Windows guide
  build/  dist/        git-ignored build output and SD-card staging
```

## Where to read next

- [docs/wiki/Home.md](docs/wiki/Home.md) — the wiki index.
- [docs/USER-GUIDE.md](docs/USER-GUIDE.md) — build and play, plain language.
- [docs/WINDOWS.md](docs/WINDOWS.md) — the native Windows build.
- [docs/bench/README.md](docs/bench/README.md) — how a performance claim is made
  and checked, with the frozen packet and claim sheets.
- [PICA200-BUILDING.md](PICA200-BUILDING.md) — what the PICA200 can and cannot do,
  what this port does today, and the bounded work packets.
- [docs/wiki/3DS-Pitfalls.md](docs/wiki/3DS-Pitfalls.md) — everything the hardware
  has broken so far, and how it was found.
- [docs/decisions.md](docs/decisions.md) — what was decided, and why.
- [gameplan.md](gameplan.md) — the plan.

## Content rules

No ROMs, no extracted assets, no saves, no golden frames, no MWCC or Nitro SDK files,
no built game binaries. The container reads your proprietary files from
`~/hgss-private`, mounted read-only. See [NOTICE.md](NOTICE.md) and
[CONTRIBUTING.md](CONTRIBUTING.md).

## License

GPL-3.0 — see [LICENSE](LICENSE). The decompilation and the game are not covered by
it, and they are not in this repo; see [NOTICE.md](NOTICE.md).
