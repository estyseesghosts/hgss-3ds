# Glossary

Terms are listed alphabetically. Each one is used in this project's docs, and the definitions are kept to what those docs support. Where a term names something the repo does not have yet, the entry says so. Pages that go deeper are linked.

## A

**ADPCM (IMA-ADPCM).** A compressed sample format that one of the DS sound channel types plays. The port's sound chip model covers it, including its loop state ([gameplan.md](../../gameplan.md), Phase 7, step 3).

**Affine.** A background or sprite that is rotated or scaled by a matrix, instead of only scrolled. The software renderer draws affine backgrounds ([3ds_port/nitro/src/port_render2d.c](../../3ds_port/nitro/src/port_render2d.c) header).

**ARM7.** The second CPU in the DS. It runs the services the game asks for: clock, touch, power, save chip, sound driver and wireless. The port replaces it with a virtual ARM7 ([Architecture](Architecture.md)).

**ARM9.** The main CPU in the DS. It runs the game and most of the SDK. On the port, the ARM9 code is compiled with GCC for the 3DS or the PC ([Architecture](Architecture.md)).

**APT.** The 3DS application-management service. The plan uses it to pause the game and audio when the HOME menu opens or the lid closes, and to delay those events during an infrared transfer ([gameplan.md](../../gameplan.md), Phases 5 and 9).

**Azahar.** A 3DS emulator. The plan uses it for fast iteration and GPU command logs, and warns that it runs the CPU slower than a New 3DS and can hide hardware-only bugs ([gameplan.md](../../gameplan.md), "Analysis and debugging"; risk 15).

## B

**BG.** A background layer of a 2D engine. Each DS 2D engine has four, and they have a priority order. Text BGs and affine BGs are the two kinds ([gameplan.md](../../gameplan.md), Phase 11).

**BizHawk.** An emulator with a Lua scripting interface. The plan uses its melonDS core to capture reference frames ([gameplan.md](../../gameplan.md), Phase 0, step 8).

**BLZ.** Nintendo's backward LZ compression. The decomp's `compstatic` tool uses it on the ARM9 binary. `tools/rom_compare.py` decodes it to compare two builds region by region ([tools/rom_compare.py](../../tools/rom_compare.py) header).

**build/upstream.** The generated checkout of the pinned decomp, with the patch stacks applied. It is git-ignored and is recreated by `tools/bootstrap.py` ([Patch-Stack](Patch-Stack.md)).

**bootstrap.** `tools/bootstrap.py`. It fetches the pinned decomp, applies the patches with `git am`, and creates the SDK symlinks ([Patch-Stack](Patch-Stack.md)).

## C

**Camera point.** A vertex that transforms exactly to clip `(0,0,0,0)` (at the camera). The DS and the PC reference put it at screen `(0,0)` and draw the triangles touching it as a fan from that corner. HGSS's title quads/quad strips use one; the PICA cannot divide by `w = 0`, so the port gives it `clip[3] = 1` in `to_screen()` ([3DS-Pitfalls](3DS-Pitfalls.md#a-camera-plane-vertex-is-a-fan-from-the-top-left-corner)).

**Capture (display capture).** A DS feature that copies the output of a 2D screen, or the 3D output, into VRAM. The plan names motion blur, encounter particles and a legendary cinematic as users. The port's version is planned in Phase 6 ([gameplan.md](../../gameplan.md), Phase 6b).

**CARD_SpiWait.** A family of SDK functions that drive the cart's SPI bus directly. The Pokéwalker infrared code uses them. The port stubs them now, with the IR chip reported as present but without a peer ([patches/NitroSDK/0701](../../patches/NitroSDK/) message).

**Checkpoint.** A 3DS save-management homebrew. The plan uses it to back up and restore saves and to dump DS cartridge saves ([gameplan.md](../../gameplan.md), "On the 3DS SD card").

**CIA.** The 3DS installable title format. The plan's release-style build is a `.cia`, which is for personal use and is never published ([gameplan.md](../../gameplan.md), Phase 5, step 8).

**Cart (cartridge).** The physical DS game card. It holds the ROM and, on HeartGold and SoulSilver, the save flash chip ([gameplan.md](../../gameplan.md), Phase 9).

**Co-Authored-By.** The commit trailer that [CONTRIBUTING.md](../../CONTRIBUTING.md) asks for on commits written with AI help. See [Contributing](Contributing.md).

**Compat header.** A small header in `hgss/compat/include/` that gives the port layer the name HGSS uses (or lacks) for something the Platinum code includes, without editing the vendored file. The directory is on the `hgss_nitro.py` include path and holds 65 files: missing ntrtwl headers, SDK stand-ins/data, ARM7 sound tables and NNS tables ([Build-Reference](Build-Reference.md#7-how-the-vendored-port-build-is-wired)).

## D

**Data pack.** The file the port reads instead of the ROM. The builder makes it from the user's own `.nds`, and it is never committed. See **pak**.

**Decomp (decompilation).** The reconstructed C source of the game. The project pins `ctrl-cheeb-del/pokeheartgold` in `upstream.lock`. Pret's `pokeheartgold` is a fallback remote, not pinned ([docs/BASE.md](../BASE.md)).

**DeSmuME.** A DS emulator. Its `.dsv` saves carry a text footer, which `savetool normalize` strips ([tools/savetool.py](../../tools/savetool.py)).

**devkitARM.** The GCC-based ARM toolchain from devkitPro. The 3DS program is built with it, with the flags `-march=armv6k -mtune=mpcore -mfpu=vfp -mfloat-abi=hard` ([gameplan.md](../../gameplan.md), Phase 2, step 8).

**devkitPro.** The package set that provides devkitARM and the 3DS libraries. The project installs its `3ds-dev` group ([docs/decisions.md](../decisions.md)).

**Dev container.** The Docker-based build environment in `.devcontainer/`. It holds the Linux toolchains and no proprietary files ([docs/devcontainer.md](../devcontainer.md)).

**DMA.** Direct memory access: a transfer the hardware runs without the CPU. The port runs DMA in software, at once or at V-blank ([3ds_port/nitro/README.md](../../3ds_port/nitro/README.md)).

**DS.** Nintendo DS, the console HeartGold and SoulSilver were made for.

**DTCM and ITCM.** Two small, fast memory blocks on the ARM9. The port accepts and ignores their setup, because the 3DS has nothing to configure ([3ds_port/nitro/README.md](../../3ds_port/nitro/README.md)).

**dsprot.** The game's copy-protection check. The plan replaces it with a stub that reports a genuine cart ([gameplan.md](../../gameplan.md), Phase 2, step 7).

## F

**Frame cap.** The shell's 60 Hz wall-clock deadline (`SYSCLOCK_ARM11 / fps`, `fps=60` default; `fps=30` is a half-speed comparison mode). The emulated DS clock is the V-blank raised once per `NitroPort_OnFrame`, not `gfxSwapBuffers` ([3DS-Pitfalls](3DS-Pitfalls.md#a-frame-under-the-refresh-is-not-a-60-hz-frame)).

**Fixture save.** A save file used as a known starting point for a scripted test, in place of a savestate. The port has no savestates ([gameplan.md](../../gameplan.md), Phase 8).

**FNT and FAT.** The file name table and the file allocation table of NitroFS. The builder reads them to find files in the ROM ([gameplan.md](../../gameplan.md), Phase 3, step 2).

**FND.** One of the NitroSystem families: memory heaps, lists, archives and allocators ([3ds_port/nitrosystem/README.md](../../3ds_port/nitrosystem/README.md)).

## G

**G2d, G3d, Snd.** The 2D, 3D and sound families of NitroSystem ([3ds_port/nitrosystem/README.md](../../3ds_port/nitrosystem/README.md)).

**G3.** The SDK's name for the DS 3D command set. Its 77 opcodes match between HGSS and Platinum's SDK ([docs/sdk-diff.md](../sdk-diff.md), section 6).

**GCC.** The GNU C compiler. The port builds with it, for the 3DS and for the PC. It differs from MWCC in several ways the patches fix ([Patch-Stack](Patch-Stack.md)).

**Golden frame.** A reference screenshot from melonDS or hardware, which a port frame is diffed against. Golden frames show game art, so they stay local ([gameplan.md](../../gameplan.md), Phase 0; "Golden diff rules").

**Golden scene.** A scripted sequence with its golden frames, used as a test. Each scene has a threshold for how many pixels may differ ([gameplan.md](../../gameplan.md), "Golden diff rules").

**GX / GXFIFO.** The 3D engine's register block and its command FIFO. The game writes 3D commands to the FIFO, and the port decodes them ([3ds_port/nitro/README.md](../../3ds_port/nitro/README.md), "Stage 1").

## H

**HBlank.** The horizontal blanking event, which fires once per scanline. The game uses it for per-line effects such as wavy screens and window wipes ([gameplan.md](../../gameplan.md), Phase 6d).

**host32.** The 32-bit host build target (`-m32 -malign-double`). It keeps pointers 4 bytes and, with patch 1001, makes struct layouts match the DS. The 64-bit `host` target is not supported by the memory-map design ([Build-Reference](Build-Reference.md#5-compiler-flags-and-abi-why-they-matter)).

**hgss_nitro.py.** `tools/hgss_nitro.py`, which builds the vendored port layer against HGSS's SDK through a shim and make overrides ([Tools-Reference](Tools-Reference.md)).

**HeartGold (HG) and SoulSilver (SS).** The two versions of the game. The US ROMs are the matching targets: HG `4fcded0e…`, SS `f8dc38ea…` ([docs/BASE.md](../BASE.md)).

**HW_ addresses.** The SDK's macros for hardware register and memory addresses. In HGSS's headers `HW_IOREG` is `0x04000000`, and the port's patches 1301/1302 redefine those macros to point at the port's arrays (an expression, `(u32)array + offset`), at the DS sizes and offsets ([docs/port-wiring.md](../port-wiring.md); [Patch-Stack](Patch-Stack.md)).

## I

**IE, IF, IME.** The DS interrupt registers: IE enables sources, IF holds the requests, and IME is the master enable. On the DS, writing 1 to IF clears that bit, which patch `0601` reproduces ([3ds_port/nitro/README.md](../../3ds_port/nitro/README.md); [patches/NitroSDK/0601](../../patches/NitroSDK/) message).

**i2c::IR.** A 3DS service that gives direct access to the infrared UART through the I²C bus. It is the fallback path for Phase 9 ([gameplan.md](../../gameplan.md), Phase 9).

**Infrared (IR).** The Pokéwalker link. The cart has an IR chip on its SPI bus. The 3DS reaches infrared through the `ir:USER` service, with `i2c::IR` as the fallback ([gameplan.md](../../gameplan.md), Phase 9).

**ir:USER.** The 3DS infrared service. The plan's first path for the Pokéwalker link ([gameplan.md](../../gameplan.md), Phase 9).

## K

**KEEP_SECTION.** A linker directive. `tools/matching-build.sh` adds five of them to the generated `main.lcf`, so the linker keeps the sections the retail ROM keeps ([tools/matching-build.sh](../../tools/matching-build.sh)).

## L

**Layout probe.** The check that compares struct sizes and member offsets between MWCC and GCC. It fails on any difference ([Save-Compatibility](Save-Compatibility.md)).

**LCF.** A linker command file. The decomp's `makelcf` step writes one from the LSF and a template. The matching build writes `build/<version>/main.lcf` in the decomp checkout ([tools/matching-build.sh](../../tools/matching-build.sh); the decomp's `common.mk`, which is not in this repo).

**LSF.** A linker spec file. The decomp's `main.lsf` (in the root of `build/upstream/` after bootstrap) lists the static `main` section, the autoloaded ITCM and DTCM sections, and the 129 overlays with their objects. `makelcf` turns it into an LCF ([gameplan.md](../../gameplan.md), Phase 1).

## M

**Manifest (manifest.json).** The file the builder writes next to the pak. It maps paths to file IDs ([gameplan.md](../../gameplan.md), Phase 3, step 3). This is planned; the builder is not in the repo.

**Matching build.** A build of the pinned decomp whose ROM SHA1 equals the retail ROM. It proves that the decomp reproduces the game, so the DS build can be the reference ([docs/BASE.md](../BASE.md); [docs/progress/phase-0.md](../progress/phase-0.md)).

**melonDS.** The DS emulator the plan treats as the reference for golden frames ([gameplan.md](../../gameplan.md), "Analysis and debugging").

**MWCC.** The Metrowerks ARM compiler that builds the retail ROM. The repo calls it `mwccarm`. Its inline `asm` blocks are why the SDK needs C versions for GCC ([docs/sdk-diff.md](../sdk-diff.md), section 2).

## N

**Noise channel.** Sound channels 14 and 15, driven by `SND_SetupChannelNoise`. After patch 1405 the port array is indexed by the DS low 16-bit address, so the control is at `0x400 + 16c`, not `16c`; patch 1407 fixes the two writes ([3DS-Pitfalls](3DS-Pitfalls.md#a-noise-note-writes-an-unused-hole)).

**NARC.** The DS's archive format for game data files. Its generated index headers have the extension `.naix` ([gameplan.md](../../gameplan.md), Phase 2, step 4).

**NAIX.** The extension of the generated NARC index headers, such as `msgdata/msg.naix` ([gameplan.md](../../gameplan.md), Phase 2, step 4; [tools/layout_probe/run_probe.py](../../tools/layout_probe/run_probe.py)).

**NDSP.** The 3DS audio API. The port sends its mixed PCM to NDSP on the device ([gameplan.md](../../gameplan.md), Phase 7).

**NitroFS.** The file system on the cartridge. It has a header, a file name table (FNT) and a file allocation table (FAT) ([gameplan.md](../../gameplan.md), Phase 3, step 2).

**NitroSDK.** Nintendo's system library between the game and the DS hardware. It covers the OS, threads, interrupts, DMA, card access, math and the PXI channel ([3ds_port/nitro/README.md](../../3ds_port/nitro/README.md)).

**NitroSystem.** Nintendo's higher-level libraries on top of NitroSDK, in the families G2d, G3d, Snd and FND ([3ds_port/nitrosystem/README.md](../../3ds_port/nitrosystem/README.md)).

**NNS.** The prefix of NitroSystem functions, such as `NNS_G2dInitCellTransferStateManager` ([patches/pokeheartgold/1101](../../patches/pokeheartgold/) message).

## O

**OAM.** Object attribute memory: the table of sprite attributes. Each entry describes one sprite (OBJ) ([gameplan.md](../../gameplan.md), NitroSystem patch list; [3ds_port/nitro/README.md](../../3ds_port/nitro/README.md)).

**OBJ.** A sprite on a DS 2D engine, described by an OAM entry.

**Overlay.** A block of code and data that the game loads on demand into a region that other overlays also use. The pinned `main.lsf` lists 129 of them. On the DS, overlays loaded into the same slot share addresses ([gameplan.md](../../gameplan.md), Phase 2, step 3).

**Overlay table.** The list the port uses to restore an overlay's data and clear its `.bss` on each load. The plan generates it from `main.lsf` ([gameplan.md](../../gameplan.md), Phase 2, step 3). This is planned.

## P

**pak.** The port's data-pack format: an archive indexed by file ID, with the original FAT IDs kept. The port reads `heartgold3ds.pak` instead of the ROM. The vendored `3ds_port/pak/` holds the reader; the HGSS builder is not written yet ([gameplan.md](../../gameplan.md), Phase 3).

**Patch stack.** The ordered set of patch files for one target: NitroSDK, NitroSystem, or the game. See [Patch-Stack](Patch-Stack.md).

**Patch-guard check.** A planned CI check that every game patch is either guarded by `SDK_PORT_3DS` or listed as a GCC-only fix. It is not in the repo yet ([gameplan.md](../../gameplan.md), "CI workflows").

**PICA200.** The 3DS GPU. The port's 3D path and the planned 2D renderer both target it ([gameplan.md](../../gameplan.md), Phase 11).

**Platinum.** The pokeplatinum-3ds port (GabeSud0), whose `3ds_port/` is vendored here. Its decomp is pret/pokeplatinum ([gameplan.md](../../gameplan.md), "What we reuse"; [platinum.lock](../../platinum.lock)).

**PORT_FIX.** The planned name for a patch that fixes a bug found in game code while porting. The plan uses it for sanitizer findings, and for mis-typed fields found in the risk register. No such patch exists yet ([gameplan.md](../../gameplan.md), Phase 4, step 6; risk 3).

**PSG.** The programmable sound generator channels of the DS: square waves on channels 8 to 13, with duty cycles, and noise on channels 14 and 15 ([gameplan.md](../../gameplan.md), Phase 7, step 3).

**PXI.** The message channel between ARM9 and ARM7. Messages carry a tag, and the port answers each tag with a handler ([3ds_port/nitro/src/pxi.c](../../3ds_port/nitro/src/pxi.c) header).

## R

**RGB555.** The 15-bit colour format the DS uses: 5 bits each for red, green and blue. The software renderer's output is RGB555 ([3ds_port/nitro/src/port_render2d.c](../../3ds_port/nitro/src/port_render2d.c) header).

**RTC.** The real-time clock. The game reads it through the ARM7, and the plan fixes it for scripted tests ([gameplan.md](../../gameplan.md), Phase 0, step 8).

## S

**savetool.** `tools/savetool.py`, which normalizes, validates and compares save images ([Save-Compatibility](Save-Compatibility.md)).

**SDK_PORT_3DS.** The macro that selects port-only code in the SDK patches and the game patches. The DS build never defines it, so the MWCC build compiles the original text ([Patch-Stack](Patch-Stack.md)).

**SDK_CW and __MWERKS__.** The macros Platinum's patches use to detect MWCC. The HGSS patches use `SDK_PORT_3DS` instead ([patches/NitroSDK/0101](../../patches/NitroSDK/) message).

**Shim (SDK shim).** The tree under `build/hgss-sdk` that `tools/hgss_nitro.py` creates: a `libraries/hgss` symlink to HGSS's SDK sources, a filtered include directory, and a stand-in `gen_fx_const.py`. It lets the vendored Makefiles build HGSS's SDK without being edited ([Build-Reference](Build-Reference.md#7-how-the-vendored-port-build-is-wired)).

**Survey.** A progress measure that counts how many files parse or compile, not a gate: `tools/sdk_compile_survey.py` (SDK) and `tools/port_compile_survey.py` (port layer) ([Tools-Reference](Tools-Reference.md)).

**Save mirror.** One of the two `0x40000` halves of the 524,288-byte save image. The game writes each chunk into both mirrors ([Save-Compatibility](Save-Compatibility.md)).

**Save slot.** One of the two save regions inside a mirror. Slot 0 starts at offset 0, and slot 1 starts at the next `0x100` boundary ([Save-Compatibility](Save-Compatibility.md)).

**Sound chip model.** The software model of the DS sound hardware in `port_sound_chip.c`. It mixes the 16 channels into PCM ([3ds_port/nitro/src/port_sound_chip.c](../../3ds_port/nitro/src/port_sound_chip.c) header).

**SPL.** The particle library that the game uses for effects, such as the opening movie and battle animations ([gameplan.md](../../gameplan.md), Phase 6e).

**Static main.** The static part of the ARM9 program. It is defined in the decomp's `main.lsf` with its own load address and stack size (`Static main` in that file).

## T

**TEV.** The texture environment stages of the PICA200 GPU, which combine colours. The plan uses them for some 2D and 3D effects ([gameplan.md](../../gameplan.md), Phase 6e and Phase 11).

**TP.** The touch screen, read through the ARM7 ([docs/port-wiring.md](../port-wiring.md), "PXI protocols differ").

**Thread (SDK thread).** A thread that NitroSDK schedules. On the port, each one runs on a real 3DS or PC thread, and only one runs game code at a time ([3ds_port/nitro/src/port_thread.c](../../3ds_port/nitro/src/port_thread.c) header).

## U

**UDS.** The 3DS local-wireless service. The plan uses it for 3DS-to-3DS trades and battles in Phase 10 ([gameplan.md](../../gameplan.md), Phase 10).

**Upstream.** The decomp that the port pins, and, when a fix is generic, the Platinum port that the fix is sent to as a pull request ([gameplan.md](../../gameplan.md), Phase 4, step 4; [CONTRIBUTING.md](../../CONTRIBUTING.md)).

## V

**V-blank.** The vertical blanking period between frames, and the interrupt that marks it. On the port, `OS_Halt` advances one frame: it calls the frame hook, starts V-blank and requests its interrupt ([3ds_port/nitro/README.md](../../3ds_port/nitro/README.md)).

**VRAM.** The DS video memory for the 2D and 3D engines. It is split into banks, A to D, that the registers map onto the engines ([gameplan.md](../../gameplan.md), Phase 6b).

**Virtual ARM7.** The port's in-process stand-in for the ARM7. It answers PXI requests for card, RTC, power, touch and sound ([3ds_port/nitro/src/port_arm7.c](../../3ds_port/nitro/src/port_arm7.c) header).

## W

**WM.** The DS wireless manager: about 40 `WM_*` functions in the game. The plan stubs them until Phase 10, then replaces them with UDS ([gameplan.md](../../gameplan.md), Phase 2, step 7 and Phase 10b).

**Wine.** A compatibility layer that runs Windows programs on Linux. The project uses WineHQ 10.0 to run MWCC and the Nitro SDK tools in the dev container ([docs/decisions.md](../decisions.md)).

**Write-through.** The save rule that each completed write reaches the file, and then is flushed, before the SDK is told it finished ([gameplan.md](../../gameplan.md), save rule 3).

## Terms that are planned

These names appear in the plan, not yet in the repo: `port_capture.c`, `port_cardspi.c`, `backend/ir_3ds.c`, `port_mic.c`, `backend/uds_3ds.c`, `port_render2d_gpu.c`, and `--dump-state`. `builder/` and `port.ini` exist now. Their roles are listed in [gameplan.md](../../gameplan.md), "HGSS-specific additions to the port layer".
