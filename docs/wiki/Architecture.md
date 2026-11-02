# Architecture

This page explains how the port is built, for a reader who has not read the code. It covers what a DS game is made of, how the Platinum port makes that run on a 3DS and on a PC, how HGSS reuses it, and what exists today against the plan.

Phase numbers (0 to 11) are the ones in [gameplan.md](../../gameplan.md). Status words mean:

- **Done**: the work is in the repo and the record shows it was checked.
- **Partial**: some of it is in the repo, and the rest is named as open in the phase records.
- **Not started**: no code or doc for it in the repo yet.

Related pages: [Patch-Stack](Patch-Stack.md), [SDK-Differences](SDK-Differences.md), [Save-Compatibility](Save-Compatibility.md), [Decisions-Explained](Decisions-Explained.md), [Glossary](Glossary.md). For setup and daily use, see [Home](Home.md), [Getting-Started](Getting-Started.md), [Daily-Workflow](Daily-Workflow.md) and [Tools-Reference](Tools-Reference.md).

## The short version

HeartGold and SoulSilver are Nintendo DS games. The project builds the game from a C source tree that reconstructs the original code. That tree is called the **decomp** (decompilation). The pinned commit is recorded in `upstream.lock`, and the matching DS build of the decomp reproduces the retail ROM byte for byte (see [Patch-Stack](Patch-Stack.md)).

The port builds the same C for a 3DS (`.3dsx`, `.cia`) and for a headless PC program. It does not rewrite the game. It keeps the game code and Nintendo's system library (the **NitroSDK**) as they are, and it supplies the DS hardware in software: memory, registers, the second CPU, 2D and 3D graphics, sound and the save chip.

That hardware layer is reused from the Platinum port, `pokeplatinum-3ds` by GabeSud0 (GPL-3.0), pinned in `platinum.lock`. Its code is written against a different SDK revision, so HGSS needs adapting work on top of it. The reuse decision is in [Decisions-Explained](Decisions-Explained.md) and in [gameplan.md](../../gameplan.md) ("What we reuse from pokeplatinum-3ds").

## What a DS game is made of

These are the parts the port has to reproduce. Each bullet names the repo file that describes it.

- **Two CPUs.** The **ARM9** runs the game and most of the system library. The **ARM7** runs the services the game asks for: the clock, the touch screen, the power chip, the save chip, the sound driver and wireless. ([gameplan.md](../../gameplan.md), "Layers", item 4; [3ds_port/nitro/src/pxi.c](../../3ds_port/nitro/src/pxi.c) header.)
- **The ARM9 program.** The linker spec `main.lsf` describes one static `main` image, the ITCM and DTCM sections that are autoloaded, and a list of **overlays**. An overlay is a block of code and data that the game loads on demand. The pinned `main.lsf` lists 129 overlays. Overlays loaded into the same slot share addresses on the DS, so code that keeps a pointer into an unloaded overlay can work there by accident. The plan lists this as a risk ([gameplan.md](../../gameplan.md), Phase 2 pitfalls and risk 5).
- **The system libraries.** **NitroSDK** is Nintendo's library between the game and the hardware: OS, threads, interrupts, DMA, card (save) access, math, and the message channel to the ARM7 (**PXI**). **NitroSystem** is a set of libraries on top of it: 2D (G2d), 3D (G3d), sound (Snd) and FND (heaps and lists). ([3ds_port/nitrosystem/README.md](../../3ds_port/nitrosystem/README.md); [gameplan.md](../../gameplan.md), layer 2.)
- **Hardware registers.** The game reads and writes fixed addresses for the display, DMA, timers, interrupt flags, the divider, and the 3D engine. The SDK wraps most of them. The DS has two screens, and each has its own 2D engine. Each screen is 256 by 192 pixels ([3ds_port/nitro/src/port_render2d.c](../../3ds_port/nitro/src/port_render2d.c) header).
- **Video memory and sprites.** **VRAM** is the video memory. It is split into banks that the registers map onto the engines. **OAM** is the table of sprite attributes. ([gameplan.md](../../gameplan.md), Phase 6b; [3ds_port/nitro/README.md](../../3ds_port/nitro/README.md), "VRAM banking".)
- **3D.** The game sends 3D commands through a FIFO to a geometry engine. The SDK's name for the command set is **G3**, and the 77 opcodes match between HGSS and the Platinum SDK ([docs/sdk-diff.md](../sdk-diff.md), section 6).
- **Sound.** A sound chip with 16 channels: PCM8, PCM16 and IMA-ADPCM sample channels, PSG square waves on channels 8 to 13, and noise on channels 14 and 15 ([gameplan.md](../../gameplan.md), Phase 7, step 3).
- **Save.** The save is a 4 Mbit flash chip on the cartridge, reached over the cart's SPI bus through the ARM7's card service. ([gameplan.md](../../gameplan.md), "Save compatibility specification"; [3ds_port/nitro/src/port_card.c](../../3ds_port/nitro/src/port_card.c) header.)

## How the port runs that on a 3DS or a PC

The idea is to leave the SDK and game code as they are, and to make each piece of hardware ordinary memory or a software model. The vendored port does this in these parts. The file names are in `3ds_port/nitro/src/` unless noted.

1. **Hardware as RAM.** The DS memory areas and I/O registers are plain arrays (`port_memory.c`, `port_ioreg.c`). The SDK's address macros point at those arrays (patches 1301/1302), so ordinary loads and stores reach them.
2. **A virtual ARM7.** `pxi.c` implements the PXI message channel. Each tag can have a port handler, and the handlers answer card, RTC, power, touch and sound requests in-process (`port_arm7.c` registers them). Answers come back through a receive queue that raises the same FIFO interrupt the DS raises.
3. **Threads.** NitroSDK keeps its own thread scheduler. Each SDK thread runs on a real thread (a 3DS thread, or a POSIX thread on the PC), and only one of them runs game code at a time. A context switch is a hand-over between those threads (`port_thread.c`).
4. **Interrupts, timers and DMA in software.** The port raises interrupts at the points the DS would, counts the four timers from a clock, and runs DMA at once or at V-blank (`port_os.c`, `port_timer.c`, `port_dma.c`). `OS_Halt`, which waits for an interrupt on the DS, advances one frame here.
5. **Software 2D.** Once per frame, the port reads the emulated register and memory state and draws both screens on the CPU, in the DS's 15-bit colour (`port_render2d.c`). The plan keeps this path as the reference until a GPU renderer matches it pixel for pixel (Phase 11).
6. **3D.** The 3D commands are decoded from the SDK's write ports, and the geometry becomes triangles (`port_gx3d*.c`). In the Platinum game build the triangles are drawn by the 3DS GPU (`game/app/port_gx3d_pica.c`, PICA200). HGSS compiles that same classic PICA backend into its 3DS shell (`hgss/game/app/main_3ds.c` starts the geometry and GPU threads). The ninth hardware run showed it drawing the title/room/field; its 3D worker was starved on the game's core (now core 2), and the title's `(0,0,0,0)` camera-plane vertices were drawn as a fan. Those triangles have zero screen area and are now dropped in `stage_triangle()`. A hardware re-run is pending. The port's own README describes the Platinum status, not HGSS's.
7. **A sound chip model.** The original ARM7 sound driver runs and writes the DS sound registers. `port_sound_chip.c` mixes the 16 channels into PCM. On the 3DS that PCM goes to NDSP, and on the PC it goes to a WAV file or a null device. The port runs HGSS's own ARM7 sources under `sub/` (patches 1401-1407), including the noise-channel register fix. `NitroPort_SoundRender` mixes 32768 Hz stereo; NitroSystem's player volume comes from `hgss/compat/src/nns_volume_table.c` (the decomp's `_0210D750` is an empty alias that would otherwise point at unrelated data), and `NitroSound_Mix` applies a 6 dB output headroom **before** its 16-bit saturation to match the DS's measured level. The 3DS shell flushes each rendered block before queuing it to NDSP and announces the DSP's native rate (~32728.5 Hz), so the DSP copies samples instead of resampling them (0.12% / two cents flat).
8. **The save.** `port_card.c` answers the cart's flash commands against an in-memory 512 KiB image and writes it to a raw `.sav` file. The format is the cart's own. See [Save-Compatibility](Save-Compatibility.md).

Two shells sit on top. `game/app/main_3ds.c` is the 3DS program (libctru, citro3d, NDSP, file system). `game/app/main_pc.c` is the headless PC program with scripted input and screenshots. Both come from Platinum. HGSS's own counterparts are `hgss/game/app/main_3ds.c` and `main_pc.c` (bring-up shells: the 3DS link and the PC link plus `--check-overlays`; rendering/sound hooks come later).

## How HGSS reuses it

- `tools/bootstrap.py` fetches the decomp at the commit in `upstream.lock` into `build/upstream/`. It then applies the patches in `patches/` with `git am`. A patch that does not apply stops the run.
- HGSS has its own NitroSDK and NitroSystem in-tree, under `lib/`. The SDK patches change them, and every change is behind the `SDK_PORT_3DS` macro, so the MWCC build is unchanged.
- Game code under `src/` changes only through `patches/pokeheartgold/`.
- `3ds_port/` is vendored from Platinum at `platinum.lock`. HGSS-only work goes into new files, or into patches meant for upstream; the one deliberate divergence is the camera-plane drop in `port_gx3d.c`'s `stage_triangle()` plus its test (see [decisions.md](../decisions.md)).
- The wiring between the vendored port and the HGSS SDK is done: all 46 port sources compile, `libnitro.a` and `libnitrosystem.a` build with zero errors, and **every** vendored suite passes (`make port-tests` exit 0; nitrosystem 17,122 checks). See [docs/port-wiring.md](../port-wiring.md), [docs/progress/agent-notes/p2-wave-a-session5.md](../progress/agent-notes/p2-wave-a-session5.md) and [docs/progress/STATE.md](../progress/STATE.md).

## Layers

```text
  +------------------------------------------------------------+
  | Platform shells    game/app/main_3ds.c, main_pc.c          |  Platinum's, vendored
  +------------------------------------------------------------+
  | Game code          src/ (decomp, C/C++)                    |  changed only by patches/pokeheartgold/
  +------------------------------------------------------------+
  | ARM9 libraries     lib/NitroSDK, lib/NitroSystem           |  changed only by patches/NitroSDK,
  |                    (MWCC inline asm in many files)          |  patches/NitroSystem, all under SDK_PORT_3DS
  +------------------------------------------------------------+
  | Port layer         3ds_port/nitro, nitrosystem, pak         |  vendored from Platinum (one recorded 3D fix)
  |                    virtual ARM7, memory, 2D, 3D, sound      |
  +------------------------------------------------------------+
  | Host                libctru + citro3d on the 3DS;           |
  |                     libc, threads and files on the PC       |
  +------------------------------------------------------------+
```

The plan also has a network layer (NitroDWC, NitroWiFi, libVCT and the lobby code) that is linked against generated "not implemented" stubs, and a data builder (`builder/`) that makes the data pack from the user's own ROM. Neither exists in the repo yet ([gameplan.md](../../gameplan.md), layers 3 and the Phase 3 section).

## Repository layout today

```text
pokeheartgold-3ds/
  upstream.lock          pinned decomp commit (ctrl-cheeb-del/pokeheartgold)
  platinum.lock          pinned Platinum port commit whose 3ds_port/ is vendored
  patches/
    NitroSDK/            15 patches (0101-0901, with gaps)
    NitroSystem/         3 patches (0301-0303)
    pokeheartgold/       60 patches (1001-1903, grouped by area)
  3ds_port/              vendored from Platinum: nitro/, nitrosystem/, pak/, game/
  hgss/compat/include/   HGSS names for headers the port includes, plus nnsys/hgss_compat.h
  hgss/compat/src/       SDK stand-ins, sound tables, NNS tables, MSL runtime
  hgss/game/             app shells (PC/3DS), stubs, game-facing glue
  tools/                 bootstrap.py, hgss_nitro.py, hgss_game.py, s_to_gas.py,
                         game_sources.py, game_overlays.py, symbol_crosscheck.py, ...
  tests/                 unit tests for tools, layout probe, sdk/ (20 host suites), game/
  docs/                  BASE.md, sdk-diff.md, port-wiring.md, decisions.md, progress/, wiki/
  .github/workflows/     matching.yml (disabled), port.yml (make test)
  build/                 generated: build/upstream, build/hgss-sdk, outputs (git-ignored volume)
```

`builder/` exists (`verify_rom.py`, `extract_rom.py`, `pack.py`). The HGSS-specific files in the plan's port table (`port_capture.c`, `port_cardspi.c`, `backend/ir_3ds.c` and the rest) do not exist yet.

## Status against the plan

| Phase | Plan exit criterion | Status | What the record shows |
| --- | --- | --- | --- |
| 0 Baseline | Matching DS build in CI; reference captures and save fixtures exist | **Partial** | The phase record says "complete except items marked NOT DONE". Both matching SHA1s are verified inside the dev container ([docs/progress/phase-0.md](../progress/phase-0.md), "Matching build: resolved"). Marked NOT DONE: golden captures (BizHawk could not run on the macOS host) and the input scripts' calibration. The record also has no save fixtures ("no saves yet") and does not report the determinism check. `matching.yml` is disabled. |
| 1 Port scaffold, SDK rebase | HGSS NitroSDK and NitroSystem compile with GCC under `SDK_PORT_3DS`; port unit tests pass | **Done** | <br>79 patches (the DS gate last ran at 78; patch 1904 is port-only). Survey: 270 of 284 SDK files parse (gcc 13; 269 with the port's gcc 10). Layout probe: 0 differences across 804 values. `make test` green (338 Python tests + 20 host suites). `make port-tests` **exit 0**: pak 120, the whole nitro sequence and nitrosystem 17,122 — all 0 failed. The DS gate at 78 reproduces both retail ROMs (HG `4fcded0e…`, SS `f8dc38ea…`) ([docs/progress/STATE.md](../progress/STATE.md)). |
| 2 Game under GCC, link | `pokeheartgold-pc` and `pokeheartgold.3dsx` link with no unresolved symbols | **Done** | 1092/1092 game files compile, 150/150 `.s`; both port archives build; **0 unresolved** on host32 and 3DS; `--check-overlays` 129/0; `symbol_crosscheck.py` missing 0. |
| 3 Data pipeline | Builder makes `heartgold3ds.pak` and `manifest.json` | **Done** | `builder/{verify_rom,extract_rom,pack}.py`; the real HG pack opens in the PC program; boot errors are mapped and tested. |
| 4 PC build reaches Oak | Scripted run from boot to the end of Oak's speech matches melonDS | **In progress** | The PC build boots through logo, movie, title, main menu, Oak's speech, naming and the first bedroom. Golden captures and the save round-trip are open; `boot_smoke.py` and scripted inputs exist. |
| 5 3DS build reaches Oak | Same run on a New 3DS | **In progress; runs on hardware** | The game boots, plays the intro, reaches Oak's speech and the bedroom/field, draws both screens with the PICA 3D layer, and plays audio. The latest artifact adds a 60 Hz cap, the NNS volume table, a 6 dB pre-saturation headroom shift, the NDSP cache flush/native rate and the camera-plane drop; its hardware re-run is pending. |
| 6 Graphics gaps | Display capture, mosaic, HBlank effects, missing 3D features | **Not started** | `docs/gfx-features.md` and `port_capture.c` are absent. The port's README lists mosaic and display capture as not drawn. |
| 7 Sound | Music, effects, cries, streams, mic | **Partial** | The genuine ARM7 driver runs in the port (1401-1407) and every vendored sound suite passes; 1407 fixes the noise-channel offsets, the NNS volume table and the 6 dB pre-saturation headroom fix the level, PSG duty 7 is silent, and the shell flushes NDSP blocks at the DSP's native rate. Streams and mic are not done. |
| 8 Gameplay | Scripted run to Falkner's badge | **Not started** | Depends on 2 to 7. |
| 9 Infrared | Pokéwalker send and receive on real hardware | **Not started** | Only the stub exists: `CARD_SpiWait*` reports "no IR peer" (NitroSDK patch 0701). No `backend/ir_3ds.c`, no `docs/ir.md`. |
| 10 Wireless | 3DS to 3DS trade and battle over UDS | **Not started** | No stub coverage test and no `docs/wireless.md`. |
| 11 GPU 2D | GPU renderer matches software pixel for pixel | **Not started** | No `docs/2d-gpu.md` and no `port_render2d_gpu.c`. |

The gameplan's Phase 1 deliverables list 27 NitroSDK and 5 NitroSystem patches to rebase from Platinum. Those were not done that way. Platinum's patches do not apply to HGSS's differently organised SDK, so the work was redone patch by patch, grouped by area. The reasons are in [Decisions-Explained](Decisions-Explained.md) and in the phase-1 record.

### Known gaps in the hardware layer for HGSS

These are the items the phase-1 record names as not done. Each one is a reason the port cannot yet run the game. Items 1-5 are now implemented (session 5) and marked accordingly; the list is kept as history where useful.

- **Done:** the header-level patches in `lib/include` (timer hooks 1402, overlay ID symbols 0603/1402, static initializers 1401).
- **Done:** the overlay hooks and libpak (0603/1402); static initializers (1401/1513/1525).
- **Done:** the 3D command ports (0604 and `g3imm`/`G3_*` forwarding in the compat header); matrix sends now reach the port's translator.
- **Done:** the ARM7 sound driver, rebuilt from `sub/` (1401), the command-flush revision fix (1404), absolute register offsets and the real volume table (1405).
- **Done:** the virtual ARM7 decoders for RTC, touch, power and sound; HGSS's literal command words are handled.
- **Done:** the vendored port's unit tests are wired to the HGSS source list (`hgss_nitro.py`), and `make port-tests` is green.
- **Done:** the two duplicate globals the game harness weakens (`FrontierScript_ReadByte`, `sListMenuTemplate`) are file-local under `SDK_PORT_3DS` (patch 1761).
- **Done:** `tools/object_aliases.py` runs after every game compile and supplies the five `gPokewalkerIr*` and destructor aliases.
- **Done:** the 3DS build's `std_sprintf`/ARM7-rule/devkitARM-only compile failures (Phase 2 link); host32 and 3DS link with 0 unresolved.
- **Open:** the gameplan's later phases: golden captures, the save round-trip, Old 3DS measurement, IR/wireless and the GPU 2D renderer.

## Notes on the record

- The phase-1 record was written when there were 11 patches; `1101`, `1201`, `1301` and `1302` were
  added later. The [STATE](../progress/STATE.md) page and the directory are current; the phase record
  carries a dated update at the top.
- The vendored port's README and header comments describe Platinum's status. They are not a claim about HGSS.
