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
6. **3D.** The 3D commands are decoded from the SDK's write ports, and the geometry becomes triangles (`port_gx3d*.c`). In the Platinum game build the triangles are drawn by the 3DS GPU (`game/app/port_gx3d_pica.c`, PICA200). The port's own README describes the Platinum status, not HGSS's.
7. **A sound chip model.** The original ARM7 sound driver runs and writes the DS sound registers. `port_sound_chip.c` mixes the 16 channels into PCM. On the 3DS that PCM goes to NDSP, and on the PC it goes to a WAV file or a null device. The plan uses HGSS's own ARM7 sources under `sub/` for the driver. That is not wired yet.
8. **The save.** `port_card.c` answers the cart's flash commands against an in-memory 512 KiB image and writes it to a raw `.sav` file. The format is the cart's own. See [Save-Compatibility](Save-Compatibility.md).

Two shells sit on top. `game/app/main_3ds.c` is the 3DS program (libctru, citro3d, NDSP, file system). `game/app/main_pc.c` is the headless PC program with scripted input and screenshots. Both come from Platinum. HGSS's own shells are planned in Phase 4 and Phase 5.

## How HGSS reuses it

- `tools/bootstrap.py` fetches the decomp at the commit in `upstream.lock` into `build/upstream/`. It then applies the patches in `patches/` with `git am`. A patch that does not apply stops the run.
- HGSS has its own NitroSDK and NitroSystem in-tree, under `lib/`. The SDK patches change them, and every change is behind the `SDK_PORT_3DS` macro, so the MWCC build is unchanged.
- Game code under `src/` changes only through `patches/pokeheartgold/`.
- `3ds_port/` is vendored byte-identical from Platinum. HGSS-only work goes into new files, or into patches meant for upstream.
- The wiring between the vendored port and the HGSS SDK is done: all 46 port sources compile, `libnitro.a` builds with zero errors, and the vendored suites run (`pak` 120 checks, `test_nitro` 702, `test_fs_pak` 120, `test_threads` 68, all 0 failed). One suite still hangs (`test_arm7`'s `test_sound`) and `test_nitrosystem` is not reached. See [docs/port-wiring.md](../port-wiring.md), [docs/progress/agent-notes/p1-runtime-bringup.md](../progress/agent-notes/p1-runtime-bringup.md) and [docs/progress/STATE.md](../progress/STATE.md).

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
  | Port layer         3ds_port/nitro, nitrosystem, pak         |  vendored byte-identical from Platinum
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
    NitroSDK/            9 patches (numbered 0101 to 0901)
    NitroSystem/         1 patch  (0301)
    pokeheartgold/       5 patches (1001, 1101, 1201, 1301, 1302)
  3ds_port/              vendored from Platinum: nitro/, nitrosystem/, pak/, game/
  hgss/compat/include/   HGSS names for headers the port includes (currently only .gitkeep)
  tools/                 bootstrap.py, hgss_nitro.py, savetool.py, layout_probe/, surveys, ...
  tests/                 unit tests for tools, layout probe, sdk/ (11 host suites), inputs/
  docs/                  BASE.md, sdk-diff.md, port-wiring.md, decisions.md, progress/, wiki/
  .github/workflows/     matching.yml (disabled), port.yml (make test)
  build/                 generated: build/upstream, build/hgss-sdk, outputs (git-ignored volume)
```

`builder/` and the HGSS-specific files in the plan's port table (`port_capture.c`, `port_cardspi.c`, `backend/ir_3ds.c` and the rest) do not exist yet.

## Status against the plan

| Phase | Plan exit criterion | Status | What the record shows |
| --- | --- | --- | --- |
| 0 Baseline | Matching DS build in CI; reference captures and save fixtures exist | **Partial** | The phase record says "complete except items marked NOT DONE". Both matching SHA1s are verified inside the dev container ([docs/progress/phase-0.md](../progress/phase-0.md), "Matching build: resolved"). Marked NOT DONE: golden captures (BizHawk could not run on the macOS host) and the input scripts' calibration. The record also has no save fixtures ("no saves yet") and does not report the determinism check. `matching.yml` is disabled. |
| 1 Port scaffold, SDK rebase | HGSS NitroSDK and NitroSystem compile with GCC under `SDK_PORT_3DS`; port unit tests pass | **Partial** | <br>39 patches in `patches/`. Survey: 270 of 284 SDK files parse (gcc 13; 269 with the port's gcc 10), up from 243. Layout probe: 0 differences across 804 values. `make test` is green (182 Python tests + 18 host suites). The vendored suites run against HGSS and pass (`pak` 120, `test_nitro` 702, `test_fs_pak` 120, `test_threads` 68 checks); `test_arm7` hangs in `test_sound`, `test_nitrosystem` is not reached. The DS gate last passed with 10 patches and has not been rerun at 39. ([docs/progress/STATE.md](../progress/STATE.md).) |
| 2 Game under GCC, link | `pokeheartgold-pc` and `pokeheartgold.3dsx` link with no unresolved symbols | **Not started** | No game object has been compiled or linked ([docs/progress/phase-1.md](../progress/phase-1.md), exit checklist). |
| 3 Data pipeline | Builder makes `heartgold3ds.pak` and `manifest.json` | **Not started** | No `builder/` directory. |
| 4 PC build reaches Oak | Scripted run from boot to the end of Oak's speech matches melonDS | **Not started** | No HGSS PC build. |
| 5 3DS build reaches Oak | Same run on a New 3DS | **Not started** | No HGSS `.3dsx`. |
| 6 Graphics gaps | Display capture, mosaic, HBlank effects, missing 3D features | **Not started** | `docs/gfx-features.md` and `port_capture.c` are absent. The port's README lists mosaic and display capture as not drawn. |
| 7 Sound | Music, effects, cries, streams, mic | **Not started** | Platinum's ARM7 sound patches (0018 to 0023) are not rebased. The mic service is not written. |
| 8 Gameplay | Scripted run to Falkner's badge | **Not started** | Depends on 2 to 7. |
| 9 Infrared | Pokéwalker send and receive on real hardware | **Not started** | Only the stub exists: `CARD_SpiWait*` reports "no IR peer" (NitroSDK patch 0701). No `backend/ir_3ds.c`, no `docs/ir.md`. |
| 10 Wireless | 3DS to 3DS trade and battle over UDS | **Not started** | No stub coverage test and no `docs/wireless.md`. |
| 11 GPU 2D | GPU renderer matches software pixel for pixel | **Not started** | No `docs/2d-gpu.md` and no `port_render2d_gpu.c`. |

The gameplan's Phase 1 deliverables list 27 NitroSDK and 5 NitroSystem patches to rebase from Platinum. Those were not done that way. Platinum's patches do not apply to HGSS's differently organised SDK, so the work was redone patch by patch, grouped by area. The reasons are in [Decisions-Explained](Decisions-Explained.md) and in the phase-1 record.

### Known gaps in the hardware layer for HGSS

These are the items the phase-1 record names as not done. Each one is a reason the port cannot yet run the game.

- The header-level patches in `lib/include`: timer hooks (Platinum 0013) and overlay ID symbols as bytes (0010).
- The overlay patches (Platinum 0011, 0014, 0027): the libpak hook and the overlay hooks, including static initializers.
- The 3D command ports (Platinum 0024 and 0025). `g3imm.c` still stores directly to the FIFO, so matrix sends do not reach the port's 3D translator.
- The ARM7 sound driver, which has to be rebuilt from `sub/` rather than rebased (Platinum 0018 to 0023).
- The PXI decoders for RTC, touch and power. HGSS sends literal command words where the port decodes named constants ([docs/port-wiring.md](../port-wiring.md), "PXI protocols differ").
- The vendored port's own unit tests are not wired to the HGSS source list. This is the open Phase 1 exit criterion.

## Notes on the record

- The phase-1 record was written when there were 11 patches; `1101`, `1201`, `1301` and `1302` were
  added later. The [STATE](../progress/STATE.md) page and the directory are current; the phase record
  carries a dated update at the top.
- The vendored port's README and header comments describe Platinum's status. They are not a claim about HGSS.
