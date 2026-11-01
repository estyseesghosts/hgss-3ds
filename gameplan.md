# PokéHeartGold Native 3DS Port — Plan

Oct 30, 2026 · @Julie

## Summary

We build a native 3DS port of HeartGold/SoulSilver from the `ctrl-cheeb-del/pokeheartgold` decompilation. We reuse the hardware layer from [GabeSud0/pokeplatinum-3ds](https://github.com/GabeSud0/pokeplatinum-3ds): a virtual ARM7, DS memory and registers as RAM, software 2D, G3→PICA200 3D, a sound chip model and a raw-`.sav` save service.

First milestone: **the headless PC build reaches Oak's speech** (Phase 4). Then **Oak's speech on a real 3DS** (Phase 5). Gameplay up to the first gym is Phase 8. Pokéwalker IR (Phase 9) is in scope. A GPU-based 2D renderer (Phase 11) is an explicit long-term goal; it replaces the software 2D we start with.

### Decisions taken

| Decision | Choice | Date |
| --- | --- | --- |
| Base for the hardware layer | Reuse pokeplatinum-3ds `3ds_port/`; contribute back where it makes sense | 2026-10-30 |
| 2D rendering, now | Their CPU software renderer, composited on the GPU | 2026-10-30 |
| 2D rendering, long term | PICA200 renderer (Phase 11), must match the software renderer pixel for pixel | 2026-10-30 |
| Save format | Unchanged 512 KB DS flash image; raw `.sav` on SD and in the CIA save archive | 2026-10-30 |
| Infrared | Implemented with the 3DS `ir:USER` service (Phase 9) | 2026-10-30 |
| Wireless and online | Stubbed; 3DS↔3DS over UDS is Phase 10; DS↔3DS judged not feasible | 2026-10-30 |
| Display at first | DS screens centered 1:1; wide-screen and remaster layouts come later, if at all | 2026-10-30 |

### Ground rules

1. **The decomp is pinned and patched, never edited in place.**
   - `upstream.lock` names the exact pokeheartgold commit.
   - All changes to game, SDK or NitroSystem code are numbered patch files in `patches/`.
   - Bumping the pin is a deliberate task: rebase the patches, then re-run every test.
2. **The DS build stays byte-matching.** A CI job builds the pinned decomp, without our patches, and checks both SHA1s. A second job checks that every game patch is either guarded by `SDK_PORT_3DS` (the macro the Platinum port uses) or listed as a GCC-only fix that MWCC would never see.
3. **The save format never changes.** The save is a 524,288-byte image with the same offsets, struct layouts and checksums. A cart save loads on the port, and a port save loads on the cart or in melonDS.
4. **No game content in the repo, in releases or in CI artifacts.** The builder makes the data pack from the user's own `.nds`. A `.3dsx` or `.cia` built from game code is for personal use only and is never published. This follows the Platinum port's policy.
5. **Every screen is checked against references.** Each test renders the same frames from the same scripted input in melonDS and in the PC build, and diffs them. 3DS hardware tests confirm performance and GPU behavior.
6. **GPL-3.0.** Reused Platinum-port code is GPL-3.0, so the HGSS port is GPL-3.0 too.

### Phases

| # | Phase | Exit criterion |
| --- | --- | --- |
| 0 | Baseline | Matching DS build in CI; reference captures and save fixtures exist |
| 1 | Port scaffold, SDK patch rebase | HGSS NitroSDK and NitroSystem compile with GCC under `SDK_PORT_3DS`; port unit tests pass |
| 2 | Game under GCC, link | `pokeheartgold-pc` and `pokeheartgold.3dsx` link with no unresolved symbols; overlay check passes |
| 3 | Data pipeline | Builder makes `heartgold3ds.pak` and `manifest.json` from a verified ROM |
| 4 | **PC build reaches Oak** | Scripted run from boot to the end of Oak's speech matches melonDS within threshold |
| 5 | **3DS build reaches Oak** | Same run on a New 3DS at full speed; save written and re-loaded |
| 6 | HGSS graphics gaps | Display capture, mosaic, HBlank effects, missing 3D features; opening movie and title match |
| 7 | Sound | Music, effects, cries, streams; Chatot recording |
| 8 | Gameplay | Scripted run to Falkner's badge, plus wild grass, trainer and gym battles, all matching |
| 9 | Infrared | Pokéwalker send and receive with real hardware |
| 10 | Wireless | 3DS↔3DS trade and battle over UDS |
| 11 | GPU 2D renderer | Software and GPU 2D match pixel for pixel on every golden scene; frame time lower |

Phases 6 and 7 can run in parallel after Phase 5. Phases 9, 10 and 11 don't block Phase 8.

## Prerequisites

Two toolchains run on one Linux machine:

- **The proprietary DS toolchain** (MWCC and the Nitro SDK tools) is used only for the matching reference build.
- **The 3DS port needs only open tools** (devkitPro, Python, Git). HGSS's NitroSDK, NitroSystem and ARM7 code already exist as C source in `lib/` and `sub/`.

Native Linux (x86-64) is the main host. The Platinum port also has macOS and Windows/MSYS2 build scripts.

### Hardware

| Item | Why | Notes |
| --- | --- | --- |
| Linux workstation, 16 GB+ RAM, 40 GB free disk | Two toolchains, wine, emulators, host and 3DS build trees, captures | A 32-bit multilib toolchain may be needed for the host build (`-m32`) |
| **New 3DS / New 2DS XL** | Main target. The Platinum port's 2D thread runs on the second core; its remaster display requires a New 3DS | Must have |
| Old 3DS / 2DS | Performance floor. The Platinum port only shows the centered display on it | Decide support in Phase 5 |
| Luma3DS custom firmware on every console | Unsigned code, Rosalina GDB stub, screenshots | Required |
| SD cards, 8 GB+ | Homebrew, data pack, saves, logs |  |
| **Original HG or SS cartridge (US)** | Your ROM dump, your real save, Phase 9 IR reference | Required |
| **Pokéwalker** (+ spare CR2032 batteries) | Phase 9 tests | Required for Phase 9 |
| DS, DS Lite or DSi | Plays the cart for save round-trips; captures real cart IR traffic in Phase 9 | Strongly recommended |
| Second New 3DS | Phase 10 UDS tests | Phase 10 only |
| IR capture rig: logic analyzer (8 channel, ≥ 24 MHz) plus an IrDA receiver module, or a USB IrDA dongle | Records cart↔Pokéwalker and 3DS↔Pokéwalker traffic | Phase 9 only |

### Files you supply

- [ ] `mwccarm` archive with the versions `INSTALL.md` lists (2.0/sp2p2), plus `license.dat`. Phase 0 only.
- [ ] Nitro SDK archive for `tools/bin` (`makelcf.exe` etc.) and the `.lcf`/response templates. Phase 0 only.
- [ ] Your `pokeheartgold.us.nds`, SHA1 `4fcded0e2713dc03929845de631d0932ea2b5a37`. Optionally `pokesoulsilver.us.nds`, SHA1 `f8dc38ea20c17541a43b58c5e6d18c1732c7e582`.
- [ ] At least one real `.sav` dumped from your cart. Keep a read-only master copy.
- [ ] `dspfirm.cdc` dumped from your own 3DS with DSP1. Without it there is no audio.
- [ ] Optional for later: a save that has Pokéwalker data in it, captured before and after a real stroll.

### Host software: DS reference build (Phase 0)

- make, git, build-essential
- binutils-arm-none-eabi
- wine
- python3
- libpng-dev, pkg-config, libpugixml-dev

### Host software: the port

- **devkitPro, `3ds-dev` package group:**
  - devkitARM
  - libctru
  - citro3d, citro2d
  - 3ds-tools (`3dsxtool`, `smdhtool`, `3dslink`)
  - `tex3ds`
  - `picasso` (PICA200 shader assembler)
- **Python 3.10+.** Python 3.12+ is needed for the animated 3D banner tool. Also pip, venv, Pillow and NumPy.
- **Host C compiler** for the PC build and tests: GCC or Clang, with 32-bit multilib (`gcc-multilib`) if the host target stays `-m32`.
- **CIA tools.** makerom and bannertool; the Platinum port's `tools/build_cia_tools.sh` builds them.
- clang-format, `quilt` or `git am`/`git format-patch` (patch-stack work), ccache.
- Docker or Podman, for the CI image with devkitPro.

### Analysis and debugging

- **melonDS** (current release): reference renderer, GDB stub on the DS side.
- **BizHawk** (melonDS core) or **DeSmuME** with Lua: frame-exact scripted input and screenshot capture.
- **Azahar** (3DS emulator): fast iteration and GPU command logs. It runs the CPU slower than a New 3DS, so measure performance on hardware.
- **gdb-multiarch**, attached to Luma3DS Rosalina's GDB stub.
- **Ghidra** (optional): compare the matching ELF to the GCC build when behavior diverges.
- objdump, `nm`, `pahole` or `llvm-dwarfdump`: symbol inventories and struct layouts.
- ImageMagick `compare` (or the port's own diff tooling): golden-image diffs.
- Audio: Audacity or SoX, to compare the PC build's `--wav` output against melonDS recordings.
- IR (Phase 9): sigrok/PulseView with a UART/IrDA decoder.

### On the 3DS SD card

- Luma3DS with Rosalina; GDB stub enabled in Rosalina settings
- Homebrew Launcher (press Y to receive `3dslink` builds)
- FBI (installs CIAs)
- Checkpoint (backs up and restores saves; dumps DS cart saves)
- GodMode9 (raw dumps, file management)
- DSP1 (dumps `dspfirm.cdc`, once)
- ftpd (moves logs, screenshots and saves)

### Reference material

- GBATEK: DS 2D and 3D engines, sound, SPI, cart SPI/IR, display capture, mosaic, windows
- 3dbrew: `ir:USER`, `nwm::UDS`, `gsp`, `fs`, `ndsp`, `mic`, `ptm`, `apt`, CIA exheader service access
- citro3d examples; PICA200 texture formats and TEV combiners; picasso syntax
- Dmitry Grinberg's Pokéwalker write-up: protocol, framing and timing
- The Platinum port's `docs/` (`REMASTER.md`, `PAK_FORMAT.md`), its patch commit messages, and its header comments (they explain each subsystem)

## What we reuse from pokeplatinum-3ds

About 60k lines of the Platinum port's `3ds_port/` are game-independent, and most of them carry over. What doesn't carry over directly:

- **The patch stacks**, which must be redone for HGSS: 27 NitroSDK patches, 5 NitroSystem patches, and about 12 GCC patches out of 58 game patches.
- **The data pipeline**, which is Platinum-specific.
- **The remaster UI**, which we leave out at first.

As of 2026-10-30 their repo has 826 commits, a v0.1.0 alpha, and is pinned to pret/pokeplatinum `c248fb3f`.

### How their port works

They emulate the DS hardware under an almost unchanged SDK, instead of rewriting SDK functions:

- DS memory regions and I/O registers are plain RAM (`port_memory.c`, `port_ioreg.c`). SDK address macros point there.
- A virtual ARM7 (`pxi.c`, `port_arm7.c`) answers the SDK's inter-CPU (PXI) messages in-process: clock, touch, power, save and sound.
- The SDK's own thread scheduler is kept. Each SDK thread runs on a host thread, and only one runs game code at a time.
- Interrupts, timers and DMA run in software.
- Each frame, renderers read the emulated hardware state: software 2D, and G3 commands translated to PICA200.

### Component map

| Component | Files | Reuse for HGSS | Expected work |
| --- | --- | --- | --- |
| Memory map, I/O registers | `port_memory.c`, `port_ioreg.c` | Direct | None |
| PXI and virtual ARM7 | `pxi.c`, `port_arm7.c`, `port_pm.c` | Direct | Check HGSS's SDK revision uses the same PXI tags and payloads |
| Threads, interrupts, timers | `port_thread.c`, `port_os.c`, `port_timer.c`, `svc.c` | Direct | HGSS creates 3 threads (sound, file loading, comm); verify each |
| DMA, memory helpers | `port_dma.c`, `mi_*.c` | Direct | None |
| Overlays | `port_overlay.c`, `tools/game_overlays.py` | Adapt | 129 HGSS overlays; table generated from `main.lsf` |
| ROM filesystem | `port_fs.c`, `pak/` | Direct | New pak build ID for HGSS |
| Save | `port_card.c` | Direct | HGSS identifies `FLASH_4MBITS_EX`, then falls back to `FLASH_4MBITS`; answer both |
| Touch, RTC | `port_tp.c`, `port_rtc.c` | Direct | None |
| Software 2D | `port_render2d.c` (\~5.3k lines) | Direct | **Add display capture and mosaic** (not implemented there) |
| 3D geometry engine | `port_gx3d*.c` | Direct | Toon/highlight, fog and shadow polygons are still on their to-do list |
| PICA200 3D backend, compositor | `port_gx3d_pica.c`, `port_compose_pica.c` | Direct, then trim | Remaster and wide paths optional at first |
| Sound chip model | `port_sound_chip.c`, `port_snd.c` | Direct | The ARM7 sound driver runs on top of it; use HGSS's own ARM7 sources |
| Upscaling, xBRZ | `port_upscale.c`, `port_xbrz.c` | Optional | — |
| Wide screen, wide art, lower-screen menu | `port_wide*.c`, `port_lower*.c` | Later or never | Tied to Platinum's UI patches |
| PC headless build | `game/app/main_pc.c` | Adapt | **This is our main test harness**: scripted input, screenshots, WAV, 3D dumps |
| 3DS shell | `game/app/main_3ds.c`, `port_pica.h`, `cia/` | Adapt | Titles, paths, CIA unique ID, banner |
| Tests | `3ds_port/*/tests/` | Direct | They run on the PC; keep them all green |
| Bootstrap, patch tooling | `tools/bootstrap.py`, `upstream.lock` | Adapt | HGSS has the SDK in-tree (`lib/`); Platinum fetches it as subprojects |
| Data builder | `builder/` | Rewrite parts | ROM checks, NitroFS extraction, and HGSS-specific generated headers |
| CI | `.github/workflows` | Adapt | Never publish game-built binaries |

### What HGSS needs that Platinum doesn't have

**Graphics:**

- **Display capture**, used by:
  - `field_motion_blur.c`
  - `encounter_effect_particles.c`
  - `legendary_cinematic.c`
- **Mosaic.**
- **HBlank-driven effects**, in:
  - the encounter transitions (`encounter_effect_*.c`)
  - `hblank_system.c`
  - `field_poison_effect.c`

**Hardware:**

- **Pokéwalker IR over the cart SPI bus** (`pokewalker_ir.c`):
  - calls `CARD_SpiWaitInit`, `CARD_SpiWaitReadRange`, `CARD_SpiWaitWriteRange`, `CARD_SpiWaitGetStatus` and `CARD_Set*WaitCycles`
  - `fieldmap.c` also probes `CARD_SpiWaitGetStatus` for `0xAA`
- **Mic:** Chatot recording, the mic test overlay, `voice_chat.c`.

**HGSS-only content:**

- Pokéathlon: heavy touch and timing use.
- Pokégear: radio, phone and map, on the bottom screen.
- 56 C++ files in `src/lobby/` (Wi-Fi Plaza), which need C++ static constructors per overlay.
- HGSS's own NitroDWC/WiFi revision, dsprot and libVCT. All are stubbed.

### Licensing and coordination

- Their original code is **GPL-3.0**; the decomp and the game are not covered by it. Reusing their code makes the HGSS port GPL-3.0, with their copyright notices kept and changes documented.

## Architecture and repository layout

The pokeheartgold source is fetched and patched at build time, then compiled together with the port layer. The game and the SDK keep talking to "hardware"; that hardware is now RAM, and the renderers and services read it each frame.

### Layers

1. **Game code**: `src/`, \~1,750 C/C++ files, plus the data `.s` files converted to C or assembled with GAS. Changed only by the patches in `patches/pokeheartgold/`.
2. **ARM9 libraries, compiled from HGSS's own sources in `lib/`**:
   - NitroSDK
   - NitroSystem (G2d, G3d, Snd, FND)
   - MSL\_C, swapped for newlib where needed
   - SPL particles
   - nitrocrypto

   Patched by `patches/NitroSDK/` and `patches/NitroSystem/`. These are rebased from Platinum's patches, not copied.
3. **Network and protection libraries**:
   - NitroDWC, NitroWiFi, libVCT and `src/lobby` link against generated "not implemented" stubs (the Platinum port's `make_game_wifi_stubs.py`).
   - dsprot is replaced by a stub that always reports a genuine cart.
4. **ARM7 side**: not run as a separate CPU. The virtual ARM7 (`pxi.c` + handlers) answers ARM9 requests. Its sound part runs the SDK's ARM7 sound-driver C code on the sound chip model. HGSS's `sub/` sources are the reference for that driver's revision.
5. **Port layer**: `3ds_port/nitro`, `nitrosystem` and `pak`, reused from Platinum. The table below adds the HGSS-specific pieces.
6. **Platform shells**:
   - `main_3ds.c`: libctru, gfx, citro3d, NDSP, fs, APT, IR, UDS.
   - `main_pc.c`: headless; files in and out.

### HGSS-specific additions to the port layer

| File | Purpose | Phase |
| --- | --- | --- |
| `port_capture.c` | Display capture: engine A output and/or VRAM → VRAM bank, with blend factors | 6 |
| `port_render2d.c` (patch) | Mosaic for BG and OBJ; capture sources | 6 |
| `port_cardspi.c` | `CARD_SpiWait*` API → IR transport; replies `0xAA` to the status probe | 2 (stub), 9 (real) |
| `backend/ir_3ds.c` | `ir:USER` bring-up, baud, send and receive ring, timing | 9 |
| `port_mic.c` | Virtual ARM7 mic → libctru `mic` service, resampled to the rate the game asks for | 7 |
| `backend/uds_3ds.c` | WM/MP-style link replaced over `nwm::UDS` | 10 |
| `port_render2d_gpu.c` + `shaders/2d*.v.pica` | Long-term GPU 2D renderer | 11 |

### Repository layout (sister-repo default)

```
pokeheartgold-3ds/
  upstream.lock            # url + commit of pokeheartgold
  platinum.lock            # url + commit of pokeplatinum-3ds whose 3ds_port/ we vendor
  patches/
    NitroSDK/              # rebased from Platinum, numbered, SDK_PORT_3DS-guarded
    NitroSystem/
    pokeheartgold/         # GCC fixes, port hooks (V-blank done, IR, capture hints)
  3ds_port/                # vendored from Platinum (or submodule), plus HGSS files
    nitro/ nitrosystem/ pak/
    game/app/{main_3ds.c, main_pc.c, heartgold.c, heartgold.h}
    game/cia/{pokeheartgold.rsf, icon, banner}
  builder/                 # verify_rom, extract_rom, pack, web (HGSS edition)
  tools/
    bootstrap.py           # fetch upstream, apply patches into build/upstream
    game_sources.py        # read HGSS Makefile/main.lsf for the file and overlay lists
    game_overlays.py       # per-overlay sections + overlay table
    make_game_wifi_stubs.py
    savetool.py            # normalize, validate, diff .sav files
    golden/                # melonDS capture scripts, diff runner
  tests/
    inputs/*.txt           # frame-indexed input scripts (no game data)
    golden/                # local only, never committed (they show game art)
  docs/
  build/                   # upstream checkout, generated files, outputs (ignored)
```

### Build targets

| Command | Output | Use |
| --- | --- | --- |
| `python tools/bootstrap.py` | `build/upstream/` patched tree + generated headers | Once, and after any lock or patch change |
| `make test` | Port unit tests, `pokeheartgold-pc`, overlay check, boot smoke test | Every change; CI |
| `make game` | `pokeheartgold.3dsx` | Hardware testing |
| `make game-cia ROM=…` | `pokeheartgold.cia` with real save archive and banner | Release-style testing |
| `make matching` (in a separate checkout) | DS ROMs + SHA1 check | CI, every upstream bump |

### Threads at run time (3DS)

| Thread | Core (New 3DS) | Does |
| --- | --- | --- |
| Game | 0 | SDK threads, one running at a time; game logic; G3 command decode |
| 3D | 0 | Builds PICA command lists and submits them |
| 2D drawing | 1, lowest priority | Software 2D of whichever screens changed |
| Audio | system (NDSP callback) | Sound chip model mixing into NDSP buffers |
| IR (Phase 9) | 1 | `ir:USER` receive events into a ring; answers within the Pokéwalker's timing |

On an Old 3DS everything shares core 0. That's why Old 3DS support is a separate decision in Phase 5.

## Phase 0: Baseline

Phase 0 produces three things: a matching DS build you trust, the reference frames and saves that every later phase is checked against, and a reply from the Platinum maintainer about how the two projects relate.

### Steps

1. **Create the port repo** (`pokeheartgold-3ds`), GPL-3.0. Copy their `NOTICE.md`, `CONTRIBUTING.md` and `AI_DISCLOSURE.md` as starting points.
2. **Clone the decomp separately.** `git clone https://github.com/ctrl-cheeb-del/pokeheartgold` with full history (no `--depth`). Also add pret/pokeheartgold as a remote, as a fallback upstream.
3. **Choose the pin.**
   1. Pick today's master commit.
   2. Write it into `upstream.lock` as `url=` and `commit=` lines, the same format as Platinum's.
   3. The fork renames types daily (BOOL/enum cleanups). Only bump the pin at phase boundaries.
4. **Install the DS toolchain** per `INSTALL.md`:
   1. MWCC to `tools/mwccarm/2.0/sp2p2/`. Run it once under wine to register the license.
   2. Nitro SDK `tools/bin` to `tools/bin`.
   3. `ARM7-TS.lcf.template` to `sub/`.
   4. `ARM9-TS.lcf.template` and `mwldarm.response.template` to the repo root.
5. **Build both versions.** `make`, then `make GAME_VERSION=SOULSILVER`. Check the README SHA1s, and time the builds.
6. **Keep the map file and symbols** for the pinned commit: `main.elf`, the link map, and `nm -n` output. Phase 2 uses them to compare symbols. Phase 1 uses `main.lsf` and the map to list overlays and their object files.
7. **CI job `matching.yml`.**
   - Runs on a self-hosted runner, or a private image holding the SDK. The SDK and MWCC must never be in a public image.
   - Builds the pinned commit and checks both SHA1s.
   - Runs on every change to `upstream.lock`.
8. **Reference captures** with BizHawk (melonDS core) or DeSmuME + Lua. Fix three things so runs repeat exactly:
   - **RTC:** 2026-01-01 10:00.
   - **Firmware profile:** name, birthday, English, MAC.
   - **Input:** frame-indexed scripts.

   Capture both screens every frame, or every N frames, for these scenes:
   - Cold boot: Game Freak logo, opening movie watched in full, title screen idle.
   - Main menu with no save, then with your save. Also the mic test (X+Y+Down on the title).
   - New Game: Oak's speech, Marill, the gender choice, naming keyboard (touch and buttons), confirm, first frames in the bedroom.
   - Bedroom → New Bark Town → Elm's lab → starter choice → first rival encounter.
   - Route 29: walking in grass until a wild battle; catch, run and faint cases.
   - First trainer battle; Violet City gym battle (from a save near Falkner).
   - Pokégear (map, phone, radio), bag, party, summary, Pokédex, PC boxes.
   - Encounter transitions (wild, trainer, legendary), poison flash, motion blur. These are the HBlank and capture cases.
   - Pokéathlon entry screen; Pokéwalker menu, without connecting.

   Store them as `tests/golden/<scene>/<frame>.png` plus `tests/inputs/<scene>.txt`. Only the input scripts are committed.
9. **Save fixtures.**
   1. Dump your cart save with Checkpoint or GodMode9.
   2. Make more saves in melonDS with the same ROM:
      - right after Oak
      - before Falkner
      - after Falkner
      - a full party and boxes
      - Pokéwalker and Pal Park data present
   3. Normalize each to raw 524,288 bytes (strip DeSmuME footers) and record the SHA1s.
   4. Keep a read-only master folder outside git.
10. **Determinism check.** Run every scene twice and confirm the frames are identical. The game seeds its RNG in `InitializeMainRNG()` (`main.c`) from the clock and ticks. If runs differ, find what you didn't fix and fix it.
11. **Clone and build the Platinum port, unchanged.** Follow its README: `python tools/bootstrap.py`, `make test`, `make game`. Run it on your New 3DS with your own Platinum data pack, if you own the cart. This confirms your devkitPro install and teaches you their workflow before you change anything.

### Deliverables

- [ ] Maintainer contacted; coordination decision recorded in the decisions log
- [ ] `pokeheartgold-3ds` repo with license files and `upstream.lock`
- [ ] Matching build of both versions, SHA1s verified; `matching.yml` green
- [ ] Map file and symbol dumps archived for the pinned commit
- [ ] Golden captures + input scripts for every scene listed above
- [ ] Save fixture set with SHA1 manifest
- [ ] Determinism check passed
- [ ] Platinum port built and run unchanged on your hardware

### Pitfalls

- **MWCC under wine asks for the license on every compile, or fails silently.** Run the `.bat` once, and pin a wine version that works.
- **Emulator timing differs from hardware by a frame or more.** Compare only emulator to emulator, or emulator to the PC build. Never compare by frame number against a real DS.
- **The fork moves fast.** A rename upstream breaks your patches even though the matching build still passes. That's why the pin moves only at phase boundaries.
- **Golden images contain game art.** They stay local, or in private CI storage. Commit only the input scripts and the diff thresholds.

## Phase 1: Port scaffold and SDK patch rebase

The goal is for HGSS's own `lib/NitroSDK` and `lib/NitroSystem` to compile with devkitARM GCC, and with host GCC for the PC build, under `SDK_PORT_3DS`. The Platinum port's unit tests must pass against them. No game code is involved yet.

### Steps

1. **Vendor `3ds_port/`.** Copy it from the Platinum port at a pinned commit (`platinum.lock`), or add it as a submodule. Keep it byte-identical at first. All HGSS changes go in new files, or in patches you plan to send upstream.
2. **Adapt `tools/bootstrap.py`.**
   - Platinum fetches NitroSDK and NitroSystem as meson subprojects. HGSS has them in-tree under `lib/`.
   - Change the fetch to: clone pokeheartgold at `upstream.lock` → `build/upstream/` → apply `patches/NitroSDK` to `build/upstream/lib/NitroSDK`, `patches/NitroSystem` to `lib/NitroSystem`, then `patches/pokeheartgold` to the rest.
   - Apply with `git am` so every patch keeps its message.
   - Fail hard if a patch doesn't apply cleanly.
3. **Compare SDK revisions.** HGSS (2009) shipped with a later NitroSDK/NitroSystem than Platinum (2008).
   1. List the files in both trees.
   2. Diff the files both have, after normalizing names.
   3. Record the differences in `docs/sdk-diff.md`. Watch for:
      - PXI tags and payload formats: card, RTC, touch, PM, sound, mic
      - `OSSystemWork` layout
      - the sound driver's shared-work struct
      - G3 command tables
      - card backup commands, including `FLASH_4MBITS_EX`
4. **Rebase the 27 NitroSDK patches one by one.** For each: apply, fix conflicts, build, and run the related port unit test. By category:
   - **GCC build switch** (`SDK_PORT_3DS`; 0001).
   - **Software divider and square root hooks** (0002). HGSS uses the same divider registers.
   - **C versions of assembly** (0003, 0006):
     - matrix functions
     - `OS_SaveContext`/`OS_LoadContext`
     - IRQ handler
     - `MI_Cpu*` copies

     Check against HGSS's `lib/NitroSDK` `.s` files. There are 3, versus the ones Platinum replaced.
   - **Checksum and narrow-varargs fixes** (0004, 0007).
   - **Port memory map and host-safe size checks** (0005, 0015): `HW_*` addresses point at port memory; arena table on 64-bit hosts.
   - **Interrupt behavior** (0008, 0012, 0017): pending IRQs taken on unmask; thread switch at the end of an interrupt; IRQ flags cleared bit by bit.
   - **Software DMA and timers** (0009, 0013).
   - **Overlays linked in, overlay IDs as bytes/constants, overlay static initializers** (0010, 0014, 0027). Needed for HGSS's 129 overlays and its C++ lobby code.
   - **libpak ROM archive** (0011).
   - **64-bit integers 4-byte aligned, as on the DS** (0016). **Critical for save layout.**
   - **Virtual ARM7 sound** (0018–0023): registers, init, command queue, alarms, pointers, shared-work sync, channel start hook.
   - **3D** (0024–0026): ARM9 3D command ports forwarded to the geometry engine; matrix-stack error bits cleared in `GXSTAT`; matrix result registers refreshed when read.
5. **Rebase the 5 NitroSystem patches.**
   - GCC build.
   - Sound capture divide-by-zero.
   - Direct GXFIFO stores forwarded to the scene.
   - Queued 3D transfers done before drawing.
   - Unused OAM entries hidden.
6. **Write new SDK patches for HGSS-only paths found in step 3.**
   - Expected: card SPI wait functions for IR (`CARD_SpiWait*`).
   - Possibly: mic PXI, `CTRDG` (cartridge slot) probes, and any PM/power calls Platinum never made.
7. **Struct-layout probe (save safety).** For every struct reachable from `SaveData` (the save-block table in `src/save.c` and its headers):
   1. Generate a C file with `const u32 probe[] = { sizeof(T), offsetof(T, f), … }`.
   2. Compile it with MWCC (wine) and with devkitARM GCC. Read the arrays from both `.o` files with objdump.
   3. Diff them. Any difference is a release blocker.

   Check in particular:
   - **Enum size.** Both must be 4 bytes. Build with `-fno-short-enums`, as Platinum does.
   - **Bitfield order and packing.**
   - **`u64`/`s64` alignment** (covered by patch 0016).
   - **Trailing padding.**
   - **`wchar_t`/`u16` string arrays.**
8. **Run the Platinum port's tests against the HGSS libraries.** `make -C 3ds_port/nitro`, `nitrosystem` and `pak`. Fix differences that come from the revision change. Never change a test just to make it pass.

### Deliverables

- [ ] `3ds_port/` vendored with `platinum.lock`
- [ ] `bootstrap.py` producing `build/upstream/` with all patches applied
- [ ] `docs/sdk-diff.md`: Platinum vs HGSS SDK differences that matter to the port
- [ ] `patches/NitroSDK/` and `patches/NitroSystem/` rebased, with messages explaining each HGSS difference
- [ ] Struct-layout probe: tool, generated probe for all save structs, report with zero differences
- [ ] Port unit tests green against HGSS libraries

### Pitfalls

- **The SDK revision gap is larger than it looks.** A struct field added to `OSSystemWork` or to the sound shared work moves every later offset. Shared-memory code then corrupts data silently. Diff struct layouts, not just source text.
- **MWCC accepts things GCC rejects or compiles differently.** Examples: implicit int, anonymous structs, `asm` function bodies, packed bitfields, enum sizes, signed `char`. Keep Platinum's flags: `-fno-short-enums -fsigned-char -fwrapv -fno-strict-aliasing -fno-delete-null-pointer-checks -fno-aggressive-loop-optimizations`.
- **The host build runs 32-bit (`-m32 -malign-double`), so pointers stay 4 bytes on the PC.** A 64-bit host build needs more patches (Platinum's 0015 hints at them). Keep `-m32` unless there's a reason not to.
- **Rebasing alone can't see a semantic conflict.** A patch can apply cleanly against a function whose behavior changed in the newer SDK. Read each hunk's surrounding code in the HGSS tree, not just the diff.

## Phase 2: Game code under GCC, overlays and link

The goal is for `pokeheartgold-pc` (host) and `pokeheartgold.3dsx` (3DS) to link with zero unresolved symbols, and for `--check-overlays` to pass. Nothing needs to run correctly yet.

### Steps

1. **Source list.** Adapt `tools/game_sources.py` to read HGSS's build files (`Makefile`, `common.mk`, `filesystem.mk`, `main.lsf`) instead of Platinum's meson files. Output:
   - every game `.c`/`.cpp`
   - the `.s` data files
   - the `lib/` sources the ROM links
   - the overlay each object belongs to
2. **The 159 remaining `.s` files.** They are data (`*_rodata.s`, `*_bss.s`, tables) plus two lines of real code. Pick one approach per file:
   - **Preferred: assemble with devkitARM `as`.** Write a `global.inc` for GAS, mapping the decomp's macros to GAS directives. Keep symbol names and alignment.
   - **Fallback: generate C.** Convert to `const` arrays with the same symbol, alignment and section, using a small script.

   Also check `lib/MSL_C/asm/*.s` (errno, strtol). Replace them with newlib or small C.
3. **Overlays.**
   1. Adapt `tools/game_overlays.py` to HGSS's 129 overlays from `main.lsf`.
   2. Generate per-overlay `.data`/`.bss` output sections and the overlay table that `port_overlay.c` uses to restore initialized data and clear `.bss` on every `FS_LoadOverlay`.
   3. Include C++ static constructors: `src/lobby/*.cpp` lives in overlays, and Platinum's patch 0027 handles overlay initializers.
4. **Generated headers.** HGSS's build generates headers and data at build time: NARC index headers (`*.naix`), message bank IDs, constants, overlay IDs, and graphics/sound IDs.
   - Port the Platinum `tools/prepare_game_*.py` scripts where the shape matches.
   - Otherwise run HGSS's own generators (in `tools/`, built from source) on the host.
   - Output goes to `build/upstream/` only, never to git.
5. **GCC fixes as patches (`patches/pokeheartgold/`).** Use Platinum's categories as a checklist; expect the same kinds of fixes in HGSS code:
   - Anonymous structs and unions GCC rejects.
   - Functions called without a declaration. Add prototypes; implicit int returns differ.
   - `static` definitions whose header declares them extern.
   - Metrowerks-only syntax: `asm` blocks, `#pragma`s, `__declspec`, inline rules (`-fgnu89-inline`).
   - Small assembly routines in C: RC4, CRC and others found in game code.
   - Function-address tables declared as integers. Make them `uintptr_t`, or proper function-pointer types.
   - **Evaluation-order bugs.** Platinum's script `GoTo`/`Call` had to read the offset first. HGSS's script engine (`scrcmd*.c`) has the same pattern. Audit every place a read with side effects appears twice in one expression.
6. **Port hooks (patches guarded by `SDK_PORT_3DS`).**
   - Tell the port when the game's V-blank work is done. This is Platinum's patch 0011; find HGSS's equivalent in `main.c` and the V-blank callbacks.
   - Main menu: skip the wireless search (Mystery Gift, Ranger and Pokéwalker peers). `Comm_LoadAndInitDwc(3)` in `main.c` must return "not 3".
   - `Save_FlashChipIsDetected`: confirm the port's card service identifies the chip on the first try.
7. **Stubs.**
   - `tools/make_game_wifi_stubs.py` generates "not implemented" functions for NitroDWC, NitroWiFi and libVCT. Each logs its first call and returns the SDK's "not available" result code.
   - A hand-written dsprot stub reports a genuine cart. The checks are in `message_format.c`, `sound_system.c` and `code_entry_states.c`.
   - `CARD_SpiWait*`: stub reporting `0xAA` status and "no peer" until Phase 9.
   - WM (40 functions): stub as "wireless off".
8. **Link both targets.**
   - Host: `-m32`, newlib-compatible headers, plus the port's host platform layer.
   - 3DS: devkitARM, `-march=armv6k -mtune=mpcore -mfpu=vfp -mfloat-abi=hard`.
   - Fix each unresolved symbol at its source. Never add a blanket stub for a game symbol.
9. **Symbol cross-check.** Compare the GCC ELF's global symbols to the matching ELF from Phase 0. Every game and SDK function in the DS build must exist in the port, apart from the replaced or stubbed list. A missing one means a source file wasn't compiled.
10. **Overlay check.** `pokeheartgold-pc --check-overlays` loads every overlay twice and checks that `.data`/`.bss` are restored each time.

### Deliverables

- [ ] `game_sources.py`, `game_overlays.py` and the generated-header scripts for HGSS
- [ ] The 159 data `.s` files building under GAS, or converted
- [ ] `patches/pokeheartgold/` GCC-fix series, each patch with a message and the MWCC behavior it reproduces
- [ ] Wi-Fi, WM, dsprot and card-SPI stubs
- [ ] Host and 3DS link clean; symbol cross-check report; overlay check green

### Pitfalls

- **Silent behavior changes, not compile errors, are the real risk.** Uninitialized locals that MWCC happened to zero, out-of-bounds reads that hit harmless memory on the DS, and signed overflow. Build with `-Wall -Wuninitialized -Wmaybe-uninitialized`, and read every warning in game code once.
- **Overlay symbol overlap.** On the DS, overlays loaded into the same slot occupy the same addresses. Some code may compare pointers or keep stale pointers into an unloaded overlay. Linking all overlays at different addresses hides these bugs until something reads freed memory. The overlay check and an ASan host build (Testing section) catch them.
- **C++ in `src/lobby`.** Build with `-fno-exceptions -fno-rtti`, and confirm global constructors run on overlay load, not at program start.
- **The GAS `global.inc` must match the original alignment exactly.** A one-word shift in a data table breaks lookups without any error.

## Phase 3: Data pipeline

The goal is a builder that turns the user's own verified `.nds` into `heartgold3ds.pak` + `manifest.json`, which the port reads instead of a ROM. No game data enters the repo or the program binary.

### Steps

1. **`builder/verify_rom.py`.**
   - Accept only known SHA1s: HG US, SS US. Add other regions later, each with its own pak build ID.
   - Reject trimmed, patched or header-modified ROMs, and say why.
2. **`builder/extract_rom.py`.** Parse the NitroFS header, FNT and FAT. Return file IDs and safe relative paths. HGSS's file table layout differs from Platinum's, so make no Platinum assumptions about paths.
3. **`builder/pack.py`.** Pack the extracted tree into the ID-indexed pak (`docs/PAK_FORMAT.md`), with a new `HEARTGOLD_PAK_BUILD_ID` (the counterpart of Platinum's `PLATINUM_PAK_BUILD_ID`). Write `manifest.json`, mapping paths to IDs.
4. **Version check at boot.** The program refuses a pak with the wrong build ID or game version (HG vs SS). It shows a clear message on screen instead of crashing.
5. **SD layout.** `sdmc:/3ds/pokeheartgold/heartgold3ds.pak`, `manifest.json`, `heartgold.sav`, `log.txt`, `port.ini`. SoulSilver uses its own folder, so saves never mix.
6. **Read cache.** Platinum measured about 1,900 small reads per field load, at \~1 ms per SD read. Its block cache fixed the resulting map-transition stalls. HGSS loads from NARCs the same way, so keep the cache. Measure field transitions in Phase 8 and tune the block size.
7. **Web builder (optional, later).** Platinum's Pyodide-based builder runs entirely in the browser. Adapt it once the Python builder is stable.
8. **CIA assets from the ROM.** Icon and banner, made from the user's cart banner (`home_menu_art.py`, `banner3d`). Never committed.
9. **Builder tests.** Run against a synthetic NitroFS image, as Platinum's `test_extract_rom.py` does. CI never sees a real ROM.

### Deliverables

- [x] `verify_rom.py`, `extract_rom.py`, `pack.py` for HG and SS US
- [x] Boot-time pak check with on-screen error
- [x] SD layout documented in `3ds_port/game/README.md` (as `docs/wiki/SD-Card-Layout.md`; see decisions)
- [x] Builder unit tests on synthetic images

### Pitfalls

- **Overlays live in the ROM too.** The port links overlay code into the program, but the game still asks the filesystem for overlay files. `FS_LoadOverlayImage` must work from the pak, or be answered from the linked image. Follow Platinum's `port_fs.c` behavior exactly.
- **The ROM's file order matters to some code** that opens files by index rather than by path. The pak must keep the original FAT IDs. That's why it's ID-indexed.
- **ROM revisions.** If a second US revision exists for HGSS, its data differs. Support exactly the SHA1s you have tested.

## Phase 4: Headless PC build reaches Oak's speech

The goal: the PC build, given a scripted input file, plays from cold boot to the end of Oak's speech. Its screenshots must match the melonDS golden frames within threshold. This is the first build with working 2D (software renderer). 3D frames (opening movie, title) are rendered on the PC by the port's reference rasterizer.

### Boot path to clear, in order

This is the order of `NitroMain()` in `src/main.c`, plus what usually breaks at each step.

1. `InitSystemForTheGame`, `InitGraphicMemory`, `InitKeypadAndTouchpad`. Breaks on: memory map, VRAM bank setup, PXI touch and PM handlers.
2. `Init3DBufferSwapRequestState`, `GF_InitRTCWork`. Breaks on: RTC PXI reply format.
3. `Main_ResetOverlayManager`, `FontWork_Init`. Breaks on: font NARC reads from the pak, and allocator differences.
4. `SaveData_New`, the first save read. Breaks on: the card backup service.
   - The SDK must identify `CARD_BACKUP_TYPE_FLASH_4MBITS_EX` or `FLASH_4MBITS`.
   - With no `.sav`, the game must see erased flash (`0xFF`) and offer New Game.
5. `InitSoundData`. Breaks on: virtual ARM7 sound init and the sound archive load. Run muted at first if needed (`--wav` off).
6. `Comm_LoadAndInitDwc(3)`. Must return "not 3" through the stubs.
7. `Save_FlashChipIsDetected`. If it returns FALSE, the game takes the "no save chip" error path. Treat that path as a test failure.
8. `OS_GetResetParameter` → `intro_title` overlay (`gApplication_IntroMovie`). Breaks on: the first overlay load, G3 init, NSBMD loading and particles.
9. Title screen → main menu (overlay 36) → New Game → `oaks_speech.c`. Breaks on: text printer, window frames, touch keyboard, OBJ animation (Marill), fades.

### Steps

1. **Run `tools/boot_smoke.py`** with a synthetic pak to find the first crash without game data. Then switch to your real pak.
2. **Run with diagnostics:** `pokeheartgold-pc heartgold3ds.pak manifest.json --frames 3000 --stop-at-missing-file --stall-seconds 5 --screenshots out/ --screenshot-every 1`.
   - A missing file means a path or ID problem from Phase 3.
   - A stall is usually a thread waiting on a reply from the virtual ARM7, or on an interrupt that never comes.
3. **Golden diff runner (`tools/golden/run.py`).**
   1. Replay `tests/inputs/<scene>.txt` with `--press FRAME:BUTTONS` and `--touch FRAME:X:Y`.
   2. Screenshot both screens.
   3. Align against the melonDS frames by finding the first matching frame, then compare.
   4. Report each frame's differing-pixel count and a heat map.

   Threshold: 2D frames match exactly; 3D frames within a small per-pixel tolerance.
4. **Fix differences at the lowest layer that explains them.** Check in this order:
   1. SDK revision difference (Phase 1 diff)
   2. Port layer bug
   3. GCC behavior difference in game code

   Write every fix as a patch with a test. When the cause is in the port layer, also open an upstream PR to the Platinum port.
5. **Save written at the end of the intro.** When the game first saves after Oak, the `.sav` it writes must load in melonDS with the original ROM, and show the same player name and gender.
6. **Sanitizer pass.** Build the host target with `-fsanitize=address,undefined`, if the 32-bit host toolchain supports it, and replay boot-to-Oak. Fix every report in game code with a `PORT_FIX` patch.

### Deliverables

- [x] Golden diff runner and report format
- [ ] Scripted boot-to-Oak run matching melonDS (2D exact, 3D within tolerance)
- [ ] First port-written save verified on melonDS
- [x] ASan/UBSan clean for the scripted run (see the report_globals note)
- [ ] Upstream PRs for generic fixes found

### Pitfalls

- **Stalls look like hangs, but the cause is a missing interrupt or reply.** The SDK often waits on a PXI acknowledgement, a V-blank count, or a DMA-done flag. Log every PXI tag, and the IRQs the game enables and waits on.
- **Text depends on the font, message banks and charmap at once.** Garbled text usually means a message-bank ID mismatch from the generated headers, not a renderer bug.
- **Touch calibration.** The port stores a calibration in firmware settings that maps screen positions to raw readings. If HGSS reads `TP_GetUserInfo` differently, every tap on the naming keyboard lands in the wrong place.
- **The opening movie can be skipped with Start.** Don't let input scripts skip it. Its 3D and particle paths are needed in Phase 6, and are cheaper to fix now while the PC build is the only target.

## Phase 5: First 3DS build — Oak's speech on hardware

The goal: on a New 3DS, the `.3dsx` boots from the Homebrew Launcher and plays the same scripted run as Phase 4 at full speed. Oak's speech must be correct on both screens, and the save it writes must load again with Continue.

### Steps

1. **3DS shell.** Adapt `main_3ds.c`:
   - Paths: `sdmc:/3ds/pokeheartgold/`.
   - App title and SMDH.
   - Crash handler: writes registers, a stack dump and the last log lines to `log.txt`.
   - Start NDSP only if `dspfirm.cdc` loaded; otherwise run silent and say so on screen.
   - Hook APT: HOME menu and sleep pause the game thread and audio.
2. **Display mode.** Start with the centered 1:1 layout:
   - Top: 256×192 inside 400×240.
   - Bottom: 256×192 inside 320×240, with touch offset by (32, 24).

   Turn off the Platinum remaster, wide and lower-menu paths for HGSS until their game-side patches exist.
3. **Screen routing.** HGSS swaps which engine is on the top screen (e.g. 3D on top in battle, on the bottom in some apps). Confirm the compositor follows `POWCNT`/`GX_SetDispSelect` every frame.
4. **Input.**
   - D-pad and circle pad → DS D-pad.
   - A/B/X/Y/L/R/Start/Select as on the DS.
   - ZL/ZR and C-stick unused.
   - Lid close = sleep, through APT.
5. **Run on hardware.**
   - Send builds with `3dslink` (Homebrew Launcher, Y).
   - Attach `gdb-multiarch` to the Rosalina GDB stub for crashes.
   - Get logs over ftpd.
6. **Performance budget.** Log frame time per subsystem: game logic, 2D drawing, 3D build/submit, audio mix.
   - Target: 59.8 Hz in menus, intro and title.
   - Platinum reports the 2D picture costing \~10 ms on the core shared with 3D, and moved 2D drawing to core 1 at lowest priority. Confirm HGSS sees the same.
7. **Old 3DS decision.** Measure the same run on an Old 3DS. Record the result in the decisions log:
   - supported (with a frame-rate note)
   - supported for menus only
   - not supported
8. **CIA build.** `make game-cia ROM=…`:
   - unique title ID
   - exheader requesting `ir:USER`, `ir:rst`, `ir:u`, `nwm::UDS`, `mic:u`
   - save archive sized for the 512 KB image
   - banner and icon from the user's cart
9. **Save round-trip on hardware:**
   1. Finish Oak's speech and save.
   2. Copy `heartgold.sav` off the SD.
   3. Load it in melonDS with the original ROM. Continue must work.
   4. Put a real cart save in place and confirm Continue shows the right trainer.

### Deliverables

- [ ] `.3dsx` and `.cia` builds (personal use only)
- [ ] Scripted boot-to-Oak run on a New 3DS, at full speed, visually correct
- [ ] Frame-time log and budget table per subsystem
- [ ] Old 3DS decision recorded
- [ ] Hardware save round-trip passed in both directions

### Pitfalls

- **Azahar hides real bugs.** Cache-coherency mistakes in GPU buffers, CPU timing, and the cost of the 2D thread on core 1 behave differently on a console. Every phase exit is judged on hardware.
- **Core 1 on the New 3DS is shared with the system** and may be time-sliced. Platinum's priority choices are tuned for this; keep them.
- **The 3DS framebuffer is rotated 90°.** That's already handled in `port_compose_pica.c`. Don't fix it twice in a new compositor path.
- **No `dspfirm.cdc` = NDSP init fails.** Never let that crash the game. Run silent with an on-screen note.

## Phase 6: HGSS graphics gaps and 3D completeness

The goal: every graphics feature HGSS uses is implemented, with a golden test for each. That means display capture, mosaic, HBlank effects, and the 3D features Platinum hasn't finished. The opening movie, title screen, encounter transitions, motion blur and legendary cinematics then match melonDS.

### 6a. Inventory what HGSS actually uses

1. Grep game code for `GX_SetCapture`/`DISPCAPCNT`, `G2_SetBG*Mosaic`/`MOSAIC`, HBlank IRQ registration, `G3X_*` toon/edge/fog/alpha-test/anti-alias/clear-image calls, `G3_PolygonAttr` flags, and display modes (`GX_DISPMODE_*`).
2. Produce `docs/gfx-features.md`: each feature, its call sites, the scene where it shows, and its status in the port.

Known starting points:

- Capture: `field_motion_blur.c`, `encounter_effect_particles.c`, `legendary_cinematic.c`.
- HBlank: `hblank_system.c`, `encounter_effect_*.c`, `field_poison_effect.c`.

### 6b. Display capture

DS behavior (GBATEK, DISPCAPCNT):

- **Source A:** engine A's output or the 3D screen.
- **Source B:** a VRAM bank or the main-memory display FIFO.
- **Modes:** A only, B only, or A·EVA + B·EVB blended.
- **Destination:** VRAM bank A–D, at an offset, in one of 4 sizes.
- It runs over one frame, and the result shows up in VRAM for later frames.

Port design:

1. `port_capture.c` runs once per frame, after the 2D picture and the 3D frame for that frame exist.
2. Source A, engine A: the software 2D output before the brightness stage. Check against GBATEK which stage capture takes.
3. Source A, 3D only: needs the 3D frame on the CPU. Use the classic path, which renders 3D to a 256×192 target and `GX_DisplayTransfer`s it to a linear buffer, for frames with capture enabled. The compose path draws 3D straight into the screen, so it can't be captured.
4. Write the result as RGB555 + alpha bit into the emulated VRAM bank, and mark it dirty. The 2D renderer then shows it like any VRAM content: a bitmap BG, or VRAM display mode.
5. **Implement VRAM display mode** (`DISPCNT` mode 2) if motion blur uses it. Platinum lists the main-memory display mode as not done; confirm whether VRAM mode is.
6. Tests: a synthetic capture test in `3ds_port/nitro/tests` (known inputs, every mode and size). Golden scenes: motion blur on a field, the encounter particle effect, a legendary cinematic.

Performance: the GPU→CPU readback stalls the 3D thread. Only do it on frames where capture is enabled, and measure the cost on hardware.

### 6c. Mosaic

- BG mosaic (text and affine) and OBJ mosaic, H and V sizes 1–16, per GBATEK.
- Includes the DS-specific OBJ mosaic behavior.
- Add to `port_render2d.c` as a patch, and offer it to the Platinum port upstream.
- Unit test against a hand-computed reference.

### 6d. HBlank-driven effects

- The Platinum port already redraws per line: each line's BG scroll and windows, as the game set them in HBlank. That's `NitroPort_HBlankLinesOnce` before the 2D picture is published.
- Check it covers everything HGSS's `hblank_system.c` changes per line:
  - BG scroll X/Y
  - affine parameters
  - window positions (circle wipes)
  - blend coefficients
  - brightness
  - possibly palette entries
- Add per-line capture of any register the port doesn't yet track.
- Golden tests: every encounter transition (wild, trainer, Rocket, frontier link, wave, wipe), and the poison flash.

### 6e. 3D completeness (G3 → PICA200)

| DS 3D feature | Status in Platinum port | HGSS use | PICA200 approach |
| --- | --- | --- | --- |
| Textured, lit polygons, DS texture formats incl. 4×4 compressed | Done | Everywhere | Existing CPU decode to tiled RGBA + texture cache |
| Edge marking | Done (GPU) | Overworld outlines | Existing |
| Fog | To do | Some routes, caves, cinematics | PICA fog LUT with depth; DS fog table → 32-entry LUT |
| Toon / highlight shading | To do | Check usage with 6a | 1D color LUT selected by vertex shade: fragment lighting LUT, or TEV with a ramp texture |
| Shadow polygons | To do (sun shadows were added separately) | Object shadows | Stencil: mask pass, then shadow pass |
| Translucent sorting, polygon IDs | Partial | Water, particles, ghosts | Sort translucent batches like the DS (by Y or manual); polygon-ID test via stencil bits |
| Alpha test, anti-aliasing, clear image/depth, w-buffer | Mostly done | Various | Alpha test native; AA via the compose path's resolution; w-buffer → depth mapping |
| 3D-rendered Pokémon sprites (`PokepicManager`, 20 files) | Works in Platinum battles | Battles, summary, evolution, hatching | Paletted textures, existing path |
| SPL particles | Works | Opening movie, battle animations, weather | Existing |

### 6f. Scenes to bring to match

In order:

1. Opening movie in full
2. Title screen (Ho-Oh/Lugia model, particles)
3. Overworld in New Bark Town with edge marking
4. Elm's lab interior
5. Encounter transitions
6. Battle intro and Pokémon sprites
7. Pokégear map
8. Motion blur
9. A legendary cinematic (from a save)

### Deliverables

- [ ] `docs/gfx-features.md` with every feature HGSS uses and its status
- [ ] Display capture: all modes and sizes, unit tests, three golden scenes
- [ ] Mosaic BG+OBJ with tests; upstream PR
- [ ] HBlank register coverage for every HGSS effect; transition goldens
- [ ] Fog, toon/highlight (if used), shadow polygons, translucency sorting
- [ ] Golden scene list above matching on the PC build and visually correct on hardware

### Pitfalls

- **Capture and the compose path clash.** Any scene that captures 3D must use the classic 256×192 3D path, so it renders at DS resolution there. Switch paths per frame, not per scene, or the picture pops.
- **Capture timing.** On the DS, capture happens during display and is visible in VRAM only after it finishes. Using the result a frame too early or too late doubles or loses motion-blur trails.
- **DS 3D on two screens.** Some games alternate 3D between screens at 30 Hz using capture. If HGSS does this anywhere (check Pokéathlon and the Pokégear map), both the capture and the screen-swap logic must handle it.
- **Exact color math.** DS blending is 6-bit per channel with specific rounding. The software 2D already does this right. Keep the GPU path out of 2D blending until Phase 11, or colors will drift.

## Phase 7: Sound, streams and mic

The goal: music, sound effects, cries, streamed audio and microphone input behave like the DS. The PC build's `--wav` output must match melonDS recordings of the same input script.

### How sound works in the port

1. The game calls NitroSystem Snd on the ARM9: sequence players, sound handles, the sound archive.
2. Requests go to the virtual ARM7 through the SDK's command queue.
3. The SDK's ARM7 sound driver, compiled from source, runs the sequencer and writes the DS's 16 sound-channel registers.
4. `port_sound_chip.c` mixes those channels, like the DS mixer, into PCM. That PCM goes to NDSP on the 3DS, or a WAV/null device on the PC.

HGSS's job is to confirm its driver revision and shared-work layout match (Phase 1 SDK diff), then cover the features HGSS uses.

### Steps

1. **Driver revision.** Compare HGSS's ARM7 sound sources (`sub/`, and the ARM7 parts of `lib/NitroSDK`) to the ones the Platinum port compiles:
   - sequence commands
   - channel allocation
   - LFO
   - envelope tables
   - the shared-work struct

   Use HGSS's version wherever they differ.
2. **Sequences and banks.** Play every BGM from the sound test or a debug hook in the PC build. Compare `--wav` against melonDS:
   - tempo
   - pitch
   - instrument envelopes
   - pan
   - volume
   - loop points
3. **Channel types.** The DS chip has:
   - PCM8, PCM16 and IMA-ADPCM sample channels
   - PSG square waves (channels 8–13, duty cycles)
   - noise (channels 14–15)

   Confirm the model covers each, including ADPCM loop-start state and PSG duty.
4. **Sound capture.** NitroSystem uses the DS's sound capture units for effects. Platinum fixed a divide-by-zero there. Check which HGSS effects use capture, e.g. reverb in caves, and test them.
5. **Streams (`NNS_SndStrm*`).** HGSS uses them (alloc, setup, start, stop, volume) for streamed audio. Candidates: Pokégear radio programs, cinematics. Implement or confirm: the stream callback timing, and buffer refill from the pak without blocking the audio thread.
6. **Cries.** Every species cry, including pitch and length changes in battle. Chatot plays the recording stored in the save.
7. **Mic (new for HGSS).**
   - Implement a virtual ARM7 mic handler: the SDK mic PXI tag, auto-sampling start/stop, sample rate, 8/12-bit format, buffer pointer updates.
   - Back it with libctru's `mic` service. Resample to the DS rate the game asked for, and convert the format.
   - Users: the Chatot Chatter recording (stored in the save), the mic test (`overlay_mic_test.c`, X+Y+Down on the title screen), `voice_chat.c` (wireless, stays stubbed).
   - Test: record on the port, play it back, save. The recording must play on the cart after transferring the save.
8. **Pacing and drift.** The DS mixer runs at a fixed rate tied to its clock; the 3DS display and NDSP clocks differ slightly.
   - Let audio run free with a small resampling correction based on NDSP buffer fill.
   - Never stall the game thread on audio.
9. **System events.**
   - HOME menu or sleep: pause NDSP, then resume without a pop.
   - Lid close: same.
   - 3DS volume slider: handled by the system.
10. **Performance.** Measure mixing cost per frame on the New 3DS and Old 3DS. Mixing 16 channels at the DS rate in C should be small, but ADPCM decode and resampling add up.

### Deliverables

- [ ] Driver revision check documented; HGSS driver in use
- [ ] BGM, SFX, cries: WAV comparisons for a fixed list (title, New Bark, Route 29, Violet, wild battle, trainer battle, gym battle, Pokémon Center, Pokégear radio)
- [ ] Streams working; capture-based effects verified
- [ ] Mic: virtual ARM7 handler + `mic` backend; Chatot recording round-trip through the save
- [ ] Audio pause/resume on HOME, sleep and lid close

### Pitfalls

- **Sample-exact WAV comparison is too strict.** Mixing order and the moment a sound starts relative to the frame differ by a few samples. Compare loudness envelopes and spectra per second, plus listening. Use exact comparison only for single-channel unit tests.
- **Mic needs exheader access** (`mic:u`) in the CIA. The `.3dsx` gets it from the Homebrew Launcher's service access. Test both.
- **The Chatot recording is save data.** Its format must stay exactly as the DS writes it: 4-bit samples, fixed length. Any resampler mistake becomes permanent in the user's save.

## Phase 8: Gameplay — overworld, grass encounters, battles, gyms

The goal: a scripted playthrough from New Game to Falkner's badge runs on the PC build with no divergence from melonDS, and plays correctly on a New 3DS. Separate scripted tests cover wild grass encounters, trainer battles and the gym battle, each starting from a fixture save.

### How to test gameplay

- **Fixed clock.** Each input script fixes the RTC, because HGSS has day/night, weekday events and phone calls. Write scripts for at least morning, day and night.
- **Fixture saves instead of savestates.** The port has no savestates. Each scenario starts from a fixture save (Phase 0) placed just before it.
- **Same RNG in both runs.** Encounter and battle results depend on the RNG. Run the same script on melonDS and the PC build, and require the same encounter species and level, the same damage rolls and the same AI choices. A difference is a logic bug, not a rendering bug.
- **A state dump per scenario.** The PC build adds `--dump-state FRAME` (new): player position, map ID, party, flags/vars hash, RNG state. Comparing these against a melonDS Lua dump finds where logic diverges, long before the screen shows it.

### 8a. Overworld

- [ ] Map loading: matrix, terrain models, buildings, collision, events. Includes field transitions between town and route (measure the stall; tune the pak cache).
- [ ] Player movement: walking, running (B), bicycle, ledges, stairs, doors, warps, ladders.
- [ ] NPCs and map objects: 2D billboard sprites in 3D, movement scripts, line-of-sight, follow behavior.
- [ ] **HGSS's own walking Pokémon** (the party lead follows the player): sprite, interaction, reactions.
- [ ] Scripts: text boxes, yes/no menus, give-item, flags, warps, cutscenes (Elm's lab, the rival theft, the Mr. Pokémon errand).
- [ ] Day/night palette tinting from the RTC; weather (rain, fog); seasonal or weekday events.
- [ ] Field moves and items: Cut, Rock Smash, Headbutt trees, Surf (later), Escape Rope, Repel.
- [ ] Bottom screen in the field: Pokégear on the touch screen (map, phone, radio), and the running-shoes and menu buttons.
- [ ] Pokémon Center healing animation; Poké Mart buy/sell; PC boxes (Bill's PC).
- [ ] Saving in the field, then Continue from that save on the port and on melonDS.

### 8b. Wild grass encounters

- [ ] Step counter and encounter rate per tile and map. Repel and ability effects.
- [ ] Encounter tables by time of day (morning/day/night): species and level ranges match the DS for fixed RNG.
- [ ] Encounter transition (Phase 6 HBlank and capture effects), then the battle intro.
- [ ] Wild battle: send-out, sprites (3D-rendered Pokepic), HP bars, touch menu on the bottom screen and buttons.
- [ ] Fight, Bag, Pokémon and Run all work. Running away succeeds or fails per the RNG.
- [ ] Catching: ball throw animation, shake count, caught message, nickname prompt, Pokédex registration, sent to box if the party is full.
- [ ] Fainting your own Pokémon, whiting out, returning to the Pokémon Center.
- [ ] EXP, level up, move learning, evolution scene (evolution uses Pokepic 3D sprites).

### 8c. Trainer battles

- [ ] Trainer sight lines, exclamation, approach, intro text.
- [ ] Trainer encounter transition and intro (trainer sprite slide-in).
- [ ] AI move choice matches the DS for fixed RNG.
- [ ] Switching, items in battle, the opponent's switch prompts (Shift mode).
- [ ] Prize money, post-battle text, flags set so the trainer won't battle again.
- [ ] Rival battle (Cherrygrove) as the first scripted trainer test.

### 8d. Gyms (target: Falkner, Violet City)

- [ ] Gym entry, gym guide text, the lift puzzle, gym trainers.
- [ ] Leader battle: music, leader AI, held items.
- [ ] Badge award, the badge case on the trainer card, TM gift, the obedience level change.
- [ ] Badge effects in the field (e.g. Flash after Falkner).
- [ ] Save after the badge, then load the save on melonDS and confirm the badge, TM and flags.

### 8e. Menus and apps used along the way

- [ ] Start menu (X): Pokédex, Pokémon, Bag, trainer card, Save, Options.
- [ ] Party: summary, item, switch, field moves.
- [ ] Bag pockets, item use, register to Y.
- [ ] Pokédex: list, entry, area, size pages.
- [ ] Options: text speed, battle scene and style, sound, button mode.

### 8f. Beyond the first gym (rolling checklist)

After Falkner, track larger systems as they come up in a full playthrough, each with a fixture save and a script:

- Surfing and water encounters; fishing; Headbutt; Rock Smash.
- Day-care and eggs; hatching (Pokepic 3D).
- Pokéathlon (touch-heavy minigames, `pokeathlon_*` rodata files).
- Safari Zone with decorations; Bug-Catching Contest.
- Battle Frontier.
- The legendary cinematics (capture).
- Kanto.
- Pal Park.

### Deliverables

- [ ] `--dump-state` in the PC build; melonDS Lua dumper; state diff tool
- [ ] Scripted New Game → Falkner run, matching on PC; played on New 3DS
- [ ] Separate scripts: wild grass (each time of day), catch, run, faint, rival battle, gym battle
- [ ] All 8a–8e checklists ticked
- [ ] Field-transition time measured on hardware within budget

### Pitfalls

- **Frame-perfect scripts break when anything shifts timing**, e.g. a load that takes one frame longer. Prefer scripts that wait for a state (map ID, text box open) over fixed frame numbers. The state dump makes that possible.
- **Logic divergence from undefined behavior** appears only in specific code paths, such as battle damage with a rare item. Run the full-battle scripts under ASan/UBSan on the host.
- **The field runs its logic at 30 Hz.** Platinum adds interpolated frames for 60 FPS. Leave that off for HGSS until everything matches, then consider it as an enhancement.
- **Phone calls and radio depend on the RTC weekday and hour.** A run that only covers one clock setting misses whole scripts.

## Phase 9: Infrared — Pokéwalker over the 3DS IR port

The goal: the port's Pokéwalker menu can send a Pokémon to a real Pokéwalker and receive it back, with steps, watts and caught items, through the 3DS's built-in IR port. The save must then load on the real cart with the same Pokéwalker state.

### What the game does today

- **One file talks to the hardware.** `src/application/pokewalker/pokewalker_ir.c` is the packet layer: checksum, XOR-0xAA framing, handshake. It reaches the hardware only through `CARD_SpiWaitInit`, `CARD_SetSpiWriteWaitCycles`, `CARD_SetSpiReadWaitCycles`, `CARD_SpiWaitReadRange`, `CARD_SpiWaitWriteRange` and `CARD_SpiWaitGetStatus`.
- **The cart does the IR.** On the cart, an IR transceiver chip sits on the same SPI bus as the save flash. Those calls move whole byte runs to and from it, and the chip does the IR modulation.
- **A presence probe.** `CARD_SpiWaitGetStatus() == 0xAA` reports that the IR chip is present. `fieldmap.c` probes it too.
- **The rest is game code.** Everything above the byte pipe is in `pokewalker_ir.c`, `pokewalker_ir_requests.c`, `pokewalker_link.c` and related files: request types, the stroll send/receive, event logs. It runs unchanged.

So the port needs a **byte pipe with the right timing**. It doesn't need a Pokéwalker protocol implementation.

### Pokéwalker link facts to confirm first

From Dmitry Grinberg's write-up: IrDA-SIR-style physical layer, 115200 baud, 8N1, packets XOR'd with 0xAA, and timing-sensitive replies. **Confirm each against your own capture (step 2) before relying on it.** Record the measured reply windows in `docs/ir.md`.

### 3DS side: two possible paths

| Path | How | Upside | Risk |
| --- | --- | --- | --- |
| A. `ir:USER` service | libctru/3dbrew `ir:USER`: shared-memory send and receive buffers, baud-rate setting; used by the Circle Pad Pro | Supported system path; no conflict with the IR sysmodule | It may add its own packet framing on the wire, or not offer 115200 baud or the needed latency. Unverified |
| B. `i2c::IR` direct | Drive the IR UART behind the I²C bus yourself: baud, IrDA mode, FIFOs, through the `i2c::IR` service (needs service access; Luma grants it) | Raw bytes, full control of timing | Must not run alongside the IR sysmodule; more low-level work; I²C bandwidth limits FIFO polling |

Try A first; fall back to B if A's wire format or timing is wrong.

### Steps

1. **Get a reference transcript.**
   1. With an IrDA receiver module on a logic analyzer (sigrok/PulseView, UART/IrDA decoder), record a full DS-cart ↔ Pokéwalker session: connect, send a Pokémon, return it.
   2. Save the decoded byte streams with timestamps as `tests/ir/*.jsonl`.
   3. This becomes the ground truth for timing and content. It holds no game data apart from your own walker's state.
2. **Measure the timing windows** from the transcript:
   - the gap between a walker packet's end and the DS reply's start
   - the walker's timeout before it gives up
   - packet sizes
3. **Spike: `irprobe` homebrew** (standalone `.3dsx`, not the game):
   1. Path A: init `ir:USER` at the target baud, send a fixed byte pattern, and record it with the analyzer. Check for headers or CRCs added on the wire.
   2. If clean: receive the walker's advertisement and answer the first handshake packet from the transcript. Measure round-trip latency.
   3. If not clean, or too slow: path B with `i2c::IR`. Configure the UART in IrDA mode at 115200 8N1, read and write FIFOs from a high-priority thread on core 1, and measure again.
   4. **Go/no-go:** the reply must start inside the measured window, with margin, in 20 of 20 tries. Record the result in the decisions log.
4. **Port transport (`backend/ir_3ds.c`).**
   - A thread pumps bytes between the IR hardware and two ring buffers.
   - Receive events carry timestamps.
   - Init and shutdown happen when the game calls `CARD_SpiWaitInit`, and when it leaves the Pokéwalker app.
5. **Port API (`port_cardspi.c`).**
   - `CARD_SpiWaitGetStatus` → `0xAA` when IR is available, else what the cart returns with no IR chip.
   - `CARD_SpiWaitWriteRange(src, len)` → queue `len` bytes for sending; returns when they're on the wire.
   - `CARD_SpiWaitReadRange(dst)` → returns bytes received since the last call, up to the packet buffer size, waiting up to the time the game's wait-cycle settings imply.
   - `CARD_Set*WaitCycles` → convert DS bus cycles to a timeout.
   - Read `pokewalker_ir.c` closely to match return values: length 0 means no data, plus any error codes.
6. **PC loopback.** In the PC build, the transport talks to a scripted fake walker that replays the reference transcript. That gives a CI test of the whole game-side flow, with no hardware.
7. **Hardware test.**
   1. Connect a real Pokéwalker to the port.
   2. Send a Pokémon, walk, then return it. Check steps, watts, items, the event log, and the Pokémon's EXP/friendship gains.
   3. Save, then load the save on the real cart and check the same state.
8. **Reverse direction.** Send from the cart, walk, then return to the port, and confirm the port accepts the walker's data.
9. **Other IR features.** Check whether anything besides the Pokéwalker uses the cart IR chip, e.g. in `field_comm_cmds.c` or Mystery Gift. Route it through the same transport if so.

### Deliverables

- [ ] `docs/ir.md`: transcript, timing windows, chosen path (A or B) and why
- [ ] `irprobe` spike with the go/no-go result
- [ ] `backend/ir_3ds.c` + `port_cardspi.c`
- [ ] PC loopback test against the transcript, in CI
- [ ] Round trip with a real Pokéwalker; cart↔port save check

### Pitfalls

- **Physical alignment and range.** The 3DS IR window is on the back edge; the cart's chip faces forward from the slot. Users must point the walker at a different spot. Document it, and show it in the UI if possible.
- **Polling latency.** If replies are late, the walker drops the link with no clear error. Use the timestamps in the receive ring to detect late replies, and log them.
- **Sleep and HOME during a transfer** corrupt the session. Block HOME and sleep while the game is in an IR transfer (APT lets the app delay them), and resume cleanly if the user insists.
- **Path B and the system's IR use.** If the IR sysmodule or the Circle Pad Pro driver is active, direct UART access conflicts with it. Release the hardware fully when leaving the Pokéwalker app.
- **The walker's battery.** A low battery causes intermittent link failures that look like timing bugs. Use a fresh battery for every timing measurement.

## Phase 10: Wireless and online

The goal: online features fail cleanly through stubs. Local trades and battles work between two 3DS consoles running the port, over UDS (the 3DS local-wireless service). Linking a DS and a 3DS is out of scope; the evaluation below says why.

### 10a. Stubs (from Phase 2 onward)

| Area | What the game sees | Result for the player |
| --- | --- | --- |
| NitroDWC / NitroWiFi (GTS, Wi-Fi battles, Wi-Fi Plaza, Mystery Gift over Wi-Fi) | Connection fails with the SDK's normal "can't connect" error | The game shows its own error message |
| WM (local wireless) before 10b | Wireless unavailable | Union Room, link trades and battles, Mystery Gift over wireless, Pokémon Ranger link, Pal Pad scan: the game's own "no partner" handling |
| `CTRDG` (GBA slot) | No cartridge | Pal Park migration from GBA games can't be used (the 3DS has no GBA slot) |
| Voice chat (`voice_chat.c`) | Off | — |

Test that every menu entry that reaches these features returns to the game cleanly. No hang, no soft-lock, no crash.

### 10b. 3DS↔3DS over UDS

On the DS, local play uses WM in "MP" mode. One parent and up to 15 children swap small fixed-size packets every frame on a strict schedule. The ARM7 runs the wireless chip, and the ARM9 talks to it through WM calls over PXI.

Plan:

1. **Inventory the WM API the game uses.** 40 `WM_*` functions across `src/` and `lib/`. List them in `docs/wireless.md`, together with the game's own comm layer above WM (`Comm_*`, union room, trade and battle link), and each one's timing expectations.
2. **Implement a virtual-ARM7 WM service** that answers the SDK's WM PXI requests:
   - Initialize, StartParent/StartScan/StartConnect
   - SetMPData and the MP receive callbacks
   - Disconnect, End

   Back it with libctru UDS:
   - parent → `udsCreateNetwork`
   - scan → `udsScanBeacons`
   - connect → `udsConnectNetwork`
   - MP data → `udsSendTo` / `udsPullPacket`
3. **Emulate MP timing.**
   - Run a per-frame exchange: the parent sends its MP frame plus the collected child data once per V-blank; children answer.
   - Buffer one frame to absorb UDS latency.
   - Deliver to the game in the order and at the cadence WM would.
4. **Beacons.**
   - Put the game's GGID and beacon user data in the UDS network's app data. The game then lists the right partners.
   - Use a port-specific UDS communication ID, so only the HGSS port sees the port.
5. **Error paths.** Disconnects, a lost partner during a trade, and too-weak signal must take the game's normal error paths. A lost link mid-trade must never write a half-done trade to either save. Test that specifically.
6. **Tests.**
   - Trade between two port consoles; check both saves on melonDS.
   - A link battle.
   - A Union Room visit.
   - The Pal Pad.
   - Mystery Gift over local wireless between two ports, if the game allows it.

Risk: UDS round-trip latency and jitter may break code that expects a reply within the same frame. If so, look for the game's timeouts and retries before changing game logic.

### 10c. DS↔3DS: not feasible (decision)

- **Protocols don't match.** The DS uses Nintendo's DS wireless protocol: raw 802.11 frames with Nintendo beacons, run by the DS's wireless firmware. Native 3DS software only gets UDS, a different protocol run by the 3DS's own wireless stack.
- **No raw Wi-Fi access.** Homebrew in native 3DS mode has no supported way to send or receive raw 802.11 frames. Doing it would mean replacing the 3DS wireless firmware or system module — a research project on its own, with no prior work to build on.
- **A relay is a different project.** A PC or Raspberry Pi with a monitor-mode Wi-Fi card could relay DS wireless frames to the 3DS over IP. That's a separate tool, not a port feature.
- **Recommended alternative for moving Pokémon between cart and port:** save-file tools on a PC (e.g. PKHeX), since the save format is identical. Document this; don't build it.

### 10d. Online (future, optional)

- Nintendo's original DS online service has been shut down.
- Community replacement servers exist for some DS games, but haven't been checked for HGSS here.
- Supporting one would mean implementing a real DWC network backend on 3DS sockets (`soc:U`). That's a separate project after everything above.

### Deliverables

- [ ] Stub coverage test: every wireless and online entry point returns cleanly
- [ ] `docs/wireless.md`: WM API inventory and timing
- [ ] Virtual-ARM7 WM over UDS: trade, battle, Union Room between two New 3DS consoles
- [ ] Mid-trade disconnect test: both saves stay consistent
- [ ] DS↔3DS decision recorded with reasons

### Pitfalls

- **Trades write the save on both sides** in a fixed order, to avoid duplicates and losses. Any change in timing between the two saves risks a cloned or lost Pokémon. Test interruptions at every step of the trade.
- **UDS needs `nwm::UDS` service access in the CIA.** Wireless must be enabled on the console, and the system wireless switch must be on.
- **Download Play-style features**, if any, can't work this way. Stub them.

## Phase 11: PICA200 2D renderer (long-term goal)

The goal: a GPU renderer for both DS 2D engines that replaces `port_render2d.c` on hardware. The software renderer stays as the reference and the fallback. Success means two things:

- Every golden scene matches the software renderer, using the rule below.
- Measured CPU time for 2D drops enough to free core 1 on the New 3DS and make Old 3DS full-speed possible.

### Why this is hard on the PICA200

- **No programmable fragment shaders.** The PICA200 has vertex (and geometry) shaders, then fixed-function texture combiners (TEV, 6 stages), alpha test, stencil, depth and blending. So the GPU can't look up a DS tile index or palette per pixel.
- **No paletted textures.** Tiles must be decoded to RGBA (or RGBA5551) on the CPU, per tile and palette pair, and cached.
- **DS compositing is per pixel and stateful:**
  - priority between 4 BGs and OBJs
  - first and second blend targets (only the layer *directly* under the top pixel can be the second target)
  - windows 0/1/OBJ/outside, each enabling layers and effects
  - brightness up/down only on first-target pixels
  - semi-transparent OBJs that always blend
  - per-line register changes
- **Exact DS color math.** 5-bit channels, EVA/EVB in 1/16 steps, clamp to 31. The GPU blends in 8-bit with its own rounding.

### Design

1. **Texture cache (CPU).** Decode 8×8 tiles to RGBA5551 or RGBA8, keyed by (VRAM address, palette, extended-palette slot, format). Reuse the 2D renderer's dirty tracking on VRAM and palettes. Pack tiles into atlas pages (1024×1024, the PICA200 maximum) in the GPU's 8×8 Morton-tiled layout.
2. **Geometry.**
   - Text BGs: one quad per visible tile (up to 33×25 per layer), from the tilemap, with flips as UV swaps.
   - Affine and bitmap BGs: decode the whole map or bitmap to a texture, and draw one quad with the affine matrix in the vertex shader.
   - OBJs: one quad each (affine, double-size, 1D/2D mapping, bitmap OBJs).
3. **Layer identity in stencil.** Each layer writes its ID (BG0–3, OBJ, backdrop) and "is first target" / "is second target" bits into the 8-bit stencil buffer. Depth holds priority, with the DS tie-breaking order.
4. **Blending in passes.**
   1. Draw everything, opaque, front-most wins by depth.
   2. For first-target pixels whose stencil says the pixel under them is a second target, redraw the first-target layer with constant-color blend factors EVA/EVB. This needs the second layer's color: draw layers back to front into an offscreen buffer, or keep a two-deep color buffer (top and below) as two render targets.
   3. Brightness: full-screen pass, stencil-limited to first-target pixels.

   Prototype both approaches to the two-deep problem in step 2. Pick by measured cost and correctness.
5. **Windows.** Rasterize window rectangles and the OBJ window into stencil bits before drawing layers. Each layer's draw tests the window bits through the stencil func. Circle wipes (HBlank windows) come from per-line window spans → stencil strips.
6. **Per-line changes (HBlank).** Group lines with identical register state into bands. Draw each band with a scissor rectangle, using that band's scroll, affine and window state. A wavy effect becomes up to 192 bands, which is a worst case to measure.
7. **Mosaic.** Draw the affected layer to an offscreen target, then sample it with nearest filtering on a grid of mosaic-sized quads. Or fall back to software for that frame (step 9).
8. **Display capture.** The GPU path renders to a texture already, so capture becomes a GPU blend into a capture texture plus a readback into emulated VRAM. That's cheaper than Phase 6's CPU path.
9. **Per-frame fallback.** Each frame, the renderer checks the 2D state for anything it doesn't support yet. If it finds any, it renders that frame in software. That lets the GPU path go live feature by feature.

### Steps

1. **Write `docs/2d-gpu.md`**: the DS compositing rules from GBATEK as a precise spec, with the exact color math.
2. **PC simulator first.** Following Platinum's `port_gx3d_gpusim.c`, write `port_render2d_gpusim.c`. It runs the same batches, stencil rules and blend factors in software on the PC, so the translation can be checked against `port_render2d.c` in CI without a 3DS.
3. **Feature order:**
   1. backdrop + one text BG
   2. 4 text BGs with priority
   3. OBJs
   4. extended palettes
   5. affine and bitmap BGs
   6. windows
   7. alpha blending
   8. brightness
   9. semi-transparent OBJs
   10. HBlank bands
   11. mosaic
   12. capture

   Each step adds unit tests (synthetic VRAM states) and turns on the golden scenes that need it.
4. **Shadow mode on hardware.** A debug build renders each frame both ways, reads back the GPU result, and compares it to the software output. It logs the first mismatching pixel with its layer and stencil state. This is slow, but finds hardware-only rounding and precision issues.
5. **Performance.** Measure CPU and GPU time per frame in the heaviest 2D scenes: Pokégear map, PC boxes, Pokéathlon, battle UI, encounter transitions. Compare against the software renderer.
6. **Switch the default** once every golden scene matches. Keep the software path selectable in `port.ini`, and as the fallback.
7. **Offer upstream.** The renderer is game-independent; the Platinum port benefits too.

### Matching rule

- **Unblended pixels:** identical in RGB555.
- **Blended or brightness-adjusted pixels:** identical as well. Configure the TEV with EVA/EVB constants pre-scaled so 8-bit math rounds to the DS result.
- If a case can't be made exact, it is listed in `docs/2d-gpu.md`, limited to ±1 in a 5-bit channel, and approved as a decision.

### Deliverables

- [ ] `docs/2d-gpu.md` spec
- [ ] `port_render2d_gpu.c`, shaders, texture-cache atlas, PC simulator with CI tests
- [ ] Per-frame fallback with a coverage log
- [ ] Shadow-mode hardware verification build
- [ ] Performance report: software vs GPU per scene, New and Old 3DS
- [ ] Default switched; software path kept as fallback

### Pitfalls

- **Tile cache churn.** Scenes that rewrite VRAM every frame (text printing, the Pokégear map, scrolling text) can cost more to re-decode and upload than to draw in software. Measure; keep those layers in software if needed (hybrid per layer).
- **Upload bandwidth.** Copies into the Morton-tiled layout and GPU cache flushes are not free. Batch uploads once per frame and flush only the dirty ranges.
- **Stencil has only 8 bits.** Layer ID (3 bits), target flags (2) and window bits (2–3) just fit. Plan the bit layout before writing code.
- **The two-deep blending problem is the core risk.** If neither approach is both exact and fast, a hybrid is the likely answer: GPU for layers and OBJs, CPU for the final blend of the few blended regions.

## Save compatibility specification

The port stores exactly what the DS cart's 4 Mbit flash holds: a 524,288-byte image, read and written at the same offsets through the same SDK backup API. A save moves between cart, emulators and the port by copying the file.

### How the game accesses the save

All of it goes through `src/save.c`, using the SDK's backup API:

- `CARD_LockBackup`/`CARD_UnlockBackup`
- `CARD_IdentifyBackup(CARD_BACKUP_TYPE_FLASH_4MBITS_EX)`, falling back to `FLASH_4MBITS`
- `CARD_ReadBackupAsync` + `CARD_WaitBackupAsync`
- `CARD_ReadBackup` (a 4-byte probe at offset 0)
- `CARD_WriteAndVerifyBackupAsync` with a callback
- `CARD_TryWaitBackupAsync`/`CARD_CancelBackupAsync`
- `CARD_GetResultCode`

The ARM9 SDK handles paging, verification and callbacks. The port's `port_card.c` plays the ARM7 side: it executes flash commands on an in-memory image and persists it.

### Storage

| Build | Location | Notes |
| --- | --- | --- |
| `.3dsx` | `sdmc:/3ds/pokeheartgold/heartgold.sav` (SoulSilver: its own folder) | Raw image; copy with ftpd or the SD card |
| `.cia` | Title save archive, file `/heartgold.sav` | Committed after every write (`FSUSER_ControlArchive` commit). Checkpoint backs it up and restores it |
| PC build | `--save FILE` | Same raw image |

### Rules

1. **Size is exactly 524,288 bytes.** On load, reject anything else, except known emulator footers, which are stripped. Never truncate or pad silently.
2. **Erased flash reads as `0xFF`.** A missing file means a fully erased chip, so the game sees "no save" and offers New Game.
3. **Write-through.**
   - Each completed write updates the in-memory image and writes the affected byte range to the file.
   - Then `fsync`, or the archive commit.
   - Only then is completion reported to the SDK.
   - A crash between writes leaves the file in a state the cart could also have been in. The game's two-slot design with checksums handles the rest.
4. **No port data inside the save.** Port settings live in `port.ini`, never in the image.
5. **Console-bound values.** The game reads `OS_GetMacAddress` (main menu, Pal Park, lobby) and `OS_GetOwnerRtcOffset` (Pal Park migration, Pokéwalker). Both come from `port.ini`, defaulting to a generated MAC and offset 0. Neither decides whether a save loads; they only fill fields the game writes.
6. **Layout equality is enforced** by Phase 1's struct-layout probe: MWCC vs GCC `sizeof`/`offsetof` for every save struct. It runs in CI.

### Import and export

- **Cart → port.** Dump with Checkpoint or GodMode9; it's a raw image already. Copy to the `.3dsx` path, or restore into the CIA archive with Checkpoint.
- **Emulator → port.** melonDS and BizHawk saves are raw. DeSmuME `.dsv` has a footer; `tools/savetool.py normalize` strips it.
- **Port → cart.** Copy the file and write it to the cart with a cart save tool, e.g. GodMode9 or Checkpoint's DS-cart restore.
- `tools/savetool.py validate` checks size and both slots' checksums using HGSS's footer format, and reports which slot is newer.

### Tests

| Test | How | Pass when |
| --- | --- | --- |
| Load real save | Each fixture → Continue on the PC build; state dump | Trainer name, ID, badges, party, location match melonDS |
| Port → melonDS | Load fixture on port, play a scripted segment, save; load result in melonDS | melonDS continues; state dump matches the port's |
| Byte-level diff | Diff before vs after a save | Only the expected slot, footer and counters changed |
| No-op save | Load, save immediately | Only the save counter and checksum change |
| Fresh game | New Game on port, save; melonDS loads it | Valid |
| Interrupted write | Kill the PC build mid-write (fault injection in `port_card.c`) | The game recovers to the previous valid slot, as on the DS |
| Pokéwalker and Chatot data | Phase 7 and 9 round-trips | Data intact on the cart |
| Layout probe | CI | Zero differences |
| Hardware round-trip | Real cart save → port on 3DS → back to cart | Loads on the cart, same state |

### Pitfalls

- **The 3DS archive commit is required.** Without it, a CIA save is lost if the console powers off before the archive closes. Commit after each completed write, not only on exit.
- **SD write speed.** Writing the whole 512 KB on every save adds visible delay. Write only the ranges the SDK wrote.
- **Two versions, separate files.** HeartGold and SoulSilver saves differ by version byte and data. Never let one build load the other's save without the same checks the cart would make.

## Testing strategy and CI

The PC build does most of the testing, compared against melonDS; hardware confirms speed and GPU behavior. CI runs only on synthetic data. Anything that uses your ROM runs locally, or on a private runner.

### Test layers

| Layer | What | Where | Data |
| --- | --- | --- | --- |
| Unit | Port layer (Platinum's suites + new: capture, mosaic, card SPI, mic, WM/UDS), builder, savetool, layout probe | Public CI | Synthetic |
| Overlay check | Every overlay loaded twice; `.data`/`.bss` restored | Public CI | None |
| Boot smoke | PC build with a synthetic pak; reports how far it gets | Public CI | Synthetic |
| Golden scenes | Scripted input → screenshots diffed against melonDS | Private runner or local | Your ROM, local goldens |
| State diff | `--dump-state` vs melonDS Lua dump per scenario | Private/local | Your ROM, fixture saves |
| Audio | `--wav` vs melonDS recording: loudness envelope + spectrum per second | Private/local | Your ROM |
| Save | Save tests from the save spec | Private/local | Fixture saves |
| Sanitizers | ASan/UBSan host build over boot-to-Oak and the battle scripts | Private/local | Your ROM |
| Matching | Pinned decomp builds byte-identical ROMs | Private runner with SDK | Proprietary toolchain |
| Hardware | Phase exit scripts on New 3DS (and Old 3DS when supported); frame-time logs | Manual | Your ROM |

### CI workflows

1. `port.yml` (public, every PR):
   - devkitPro container
   - bootstrap
   - `make test`
   - `make game` (built, never uploaded)
   - patch-guard check: every game patch is `SDK_PORT_3DS`-guarded or listed as a GCC-only fix
   - layout probe on the GCC side, against a committed MWCC probe result
2. `matching.yml` (private runner): on `upstream.lock` changes.
3. `golden.yml` (private runner, nightly): golden scenes, state diffs, save tests and audio checks; uploads reports only, never images.

### Golden diff rules

- **2D-only frames: exact.**
- **Frames with 3D: per-pixel tolerance.** Thresholds are set per scene in `tests/golden/thresholds.toml`, starting at a small value. A raised threshold needs a reason in the commit.
- **Alignment.** Find the first frame where both runs match, then compare frame by frame. Report the first diverging frame, the share of differing pixels, and a heat map.
- **Upscaled or remaster output is never compared.** Comparisons use the DS-resolution path only.

### Hardware testing checklist per phase exit

- [ ] Run the phase's scripts on a New 3DS from the `.3dsx` and the `.cia`
- [ ] Frame-time log within budget
- [ ] HOME menu, sleep (lid), power off mid-session, and SD removal all handled
- [ ] Save round-trip with the cart
- [ ] `log.txt` free of warnings for the run

## Pitfalls and risks register

The biggest risks are silent ones: wrong-but-plausible behavior from SDK revision differences, compiler differences and decomp typing. The register lists each risk, where it bites, and the check that catches it.

| # | Risk | Likelihood | Impact | Phase | Mitigation |
| --- | --- | --- | --- | --- | --- |
| 1 | HGSS's newer NitroSDK/NitroSystem differs from Platinum's in shared-memory structs, PXI payloads or command tables | High | Silent corruption, hangs | 1 | `docs/sdk-diff.md`; struct-layout diff, not just a text diff; read every rebased hunk in context |
| 2 | Save struct layout differs under GCC (enum size, bitfields, `u64` alignment, padding) | Medium | Saves unusable across cart and port | 1, all | Layout probe in CI; `-fno-short-enums`; SDK patch 0016 |
| 3 | Decomp types are "wrong but matching": MWCC emits the same code for a mistyped field, GCC doesn't (signedness, width, bool vs u8) | Medium | Logic bugs in rare paths | 2, 8 | State diffs against melonDS; sanitizers; fix types with `PORT_FIX` patches and report them upstream |
| 4 | Undefined behavior MWCC tolerated: uninitialized locals, out-of-bounds reads, evaluation order | High | Rare crashes, divergence | 2, 4, 8 | Warnings audit; ASan/UBSan host runs; Platinum's script GoTo/Call fix as a pattern |
| 5 | Overlays share addresses on the DS; stale pointers into unloaded overlays work there by accident | Medium | Crashes after scene changes | 2, 8 | Overlay check; ASan; poison an overlay's `.data`/`.bss` on unload in debug builds |
| 6 | Upstream decomp churn (daily renames) breaks patches | High | Lost time | All | Pin; bump only at phase boundaries; small, focused patches |
| 7 | Vendored `3ds_port/` drifts from the Platinum port | Medium | Fixes duplicated or lost | All | `platinum.lock`; generic fixes go upstream as PRs; periodic re-sync |
| 8 | Display capture needs the 3D frame on the CPU; readback stalls | Medium | Frame drops in motion blur and transitions | 6 | Classic 3D path only on capture frames; measure; GPU-side capture in Phase 11 |
| 9 | 2D software render + capture + 3D exceed New 3DS budget in heavy scenes | Medium | Slowdown | 5, 6, 8 | Per-subsystem frame timing; dirty-region drawing; Phase 11 GPU 2D |
| 10 | Old 3DS too slow | High | Smaller audience | 5 | Explicit decision; menus-only or unsupported is acceptable |
| 11 | 3DS IR can't produce raw IrDA bytes at 115200 with the Pokéwalker's reply timing | Medium | Phase 9 blocked | 9 | Two paths (`ir:USER`, `i2c::IR`); `irprobe` go/no-go before game integration |
| 12 | UDS latency/jitter breaks MP timing assumptions | Medium | Link desyncs | 10 | One-frame buffering; use the game's own retry paths; heavy disconnect testing |
| 13 | Trades interrupted mid-save clone or lose Pokémon | Low | Data loss | 10 | Disconnect tests at every trade step; both saves checked |
| 14 | Golden tests flaky from emulator timing | Medium | Noise, ignored failures | 4+ | Align on content, not frame numbers; state-based waits in scripts |
| 15 | Azahar differs from hardware | High | Bugs found late | 5+ | Every phase exit judged on hardware |
| 16 | SD read latency during field loads | Medium | Stutter on map changes | 3, 8 | Pak block cache (Platinum measured \~1,900 reads per field load) |
| 17 | Game content leaks into the repo, CI artifacts or releases | Low | Takedown, project loss | All | Policy from Platinum's `CONTRIBUTING.md`; CI never handles ROM data; goldens stay private |
| 18 | AI-written code that doesn't follow repo conventions | Medium | Review load, regressions | All | Small PRs; header comments as in Platinum; CI patch-guard and style checks; human hardware test before merge |
| 19 | RTC-dependent content missed by tests | Medium | Untested scripts (phone, radio, events) | 8 | Scripts at morning/day/night and several weekdays |
| 20 | Mic and Chatot data mis-encoded | Low | Bad data in user saves | 7 | Round-trip through the cart before enabling |
| 21 | Pokéathlon touch timing differs (touch sampling rate, latency) | Medium | Unfair minigames | 8 | Match the SDK's touch sampling cadence; test on hardware |

## Open questions and decisions log

### Open questions

- [ ] Does the Platinum maintainer want HGSS inside their repo, or a sister repo? (Phase 0)
- [ ] Which pokeheartgold commit do we pin? ctrl-cheeb-del's master, or pret's, for slower churn? (Phase 0)
- [ ] How far is HGSS's NitroSDK/NitroSystem revision from Platinum's, and which shared structs changed? (Phase 1)
- [ ] Do the 159 data `.s` files assemble under GAS with a new `global.inc`, or do they need converting to C? (Phase 2)
- [ ] Is Old 3DS supported, and at what level? (Phase 5)
- [ ] Does HGSS use VRAM display mode, main-memory display mode, toon/highlight, or 3D on both screens anywhere? (Phase 6)
- [ ] Which HGSS audio uses streams, and which uses sound capture effects? (Phase 7)
- [ ] Can `ir:USER` send raw bytes at 115200 with no framing, or is `i2c::IR` needed? Does either meet the Pokéwalker's reply window? (Phase 9)
- [ ] Does anything besides the Pokéwalker use the cart IR chip? (Phase 9)
- [ ] Can UDS latency carry WM MP timing without game changes? (Phase 10)
- [ ] Which Platinum remaster features (wide screen, bottom menu, 60 FPS interpolation, followers) do we want for HGSS, if any? HGSS already has walking Pokémon. (After Phase 8)
- [ ] Region support beyond US HG/SS? (After Phase 8)

### Decisions log

| Date | Decision | Reason |
| --- | --- | --- |
| 2026-10-30 | Base the port on pokeplatinum-3ds's `3ds_port/` | Its hardware layer is game-independent, tested on hardware, and HGSS is its stated target |
| 2026-10-30 | Use the software 2D renderer for now | Proven; fastest path to Oak's speech and gameplay |
| 2026-10-30 | A PICA200 2D renderer is an explicit long-term goal (Phase 11) | Owner's goal; frees CPU; may enable Old 3DS |
| 2026-10-30 | Save format unchanged; raw 512 KB image | Cart ↔ port ↔ emulator interchange |
| 2026-10-30 | Pokéwalker IR is in scope, through the 3DS IR port | Owner's requirement |
| 2026-10-30 | Online stubbed; 3DS↔3DS over UDS later; DS↔3DS not pursued | Protocol mismatch; no raw 802.11 access in native 3DS mode |
| 2026-10-30 | GPL-3.0 | Required by reuse of Platinum port code |
